#include "pg_store.hpp"

#include <cstdio>
#include <stdexcept>

namespace pgstore {

namespace {

// Полнотекст (D4): вопрос разбирается двумя конфигурациями — russian (стемы,
// без стоп-слов) и simple (точные идентификаторы и пути вроде /api/mcp/ или
// aud); из simple берутся только лексемы с латиницей или цифрами, иначе
// «в», «как» и прочие русские служебные слова совпали бы почти с каждым
// чанком и отказ «этого нет» не наступал бы никогда. Лексемы объединяются
// через ИЛИ: вопрос своими словами редко содержит все слова чанка.
constexpr const char* kTextSql = R"SQL(
WITH q AS (
    SELECT to_tsquery('simple', coalesce(string_agg(quote_literal(l), ' | '), '')) AS tq
    FROM (
        SELECT unnest(tsvector_to_array(to_tsvector('russian', $1))) AS l
        UNION
        SELECT l FROM unnest(tsvector_to_array(to_tsvector('simple', $1))) AS l
        WHERE l ~ '[a-z0-9]'
    ) lexemes
)
SELECT d.id, d.repo, d.path, d.kind, d.capability, d.heading, d.content
FROM doc_chunks d, q
WHERE d.tsv @@ q.tq
  AND ts_rank(d.tsv, q.tq) > 0
  AND ($2::boolean OR d.repo = ANY($3::text[]))
  AND d.kind = ANY($4::text[])
ORDER BY ts_rank(d.tsv, q.tq) DESC, d.id
LIMIT $5
)SQL";

// Вектор (D3, D4): точный скан с фильтром области и порогом расстояния.
constexpr const char* kVectorSql = R"SQL(
SELECT d.id, d.repo, d.path, d.kind, d.capability, d.heading, d.content
FROM doc_chunks d
WHERE d.embedding IS NOT NULL
  AND ($2::boolean OR d.repo = ANY($3::text[]))
  AND d.kind = ANY($4::text[])
  AND (d.embedding <=> $1::vector) <= $5::float8
ORDER BY d.embedding <=> $1::vector, d.id
LIMIT $6
)SQL";

constexpr const char* kInsertSql = R"SQL(
INSERT INTO doc_chunks (repo, path, kind, capability, heading, content, content_hash, embedding)
SELECT $1, u.path, u.kind, u.capability, u.heading, u.content, u.content_hash, u.embedding::vector
FROM unnest($2::text[], $3::text[], $4::text[], $5::text[], $6::text[], $7::text[], $8::text[])
    AS u(path, kind, capability, heading, content, content_hash, embedding)
)SQL";

std::vector<store::Hit> to_hits(const db::Result& r) {
    std::vector<store::Hit> out;
    out.reserve(r.rows.size());
    for (const db::Row& row : r.rows) {
        store::Hit h;
        h.id = std::stoll(row.fields.at(0));
        h.repo = row.fields.at(1);
        h.path = row.fields.at(2);
        h.kind = row.fields.at(3);
        h.capability = row.fields.at(4);
        h.heading = row.fields.at(5);
        h.content = row.fields.at(6);
        out.push_back(std::move(h));
    }
    return out;
}

std::string text_array(const std::vector<std::string>& items) {
    std::vector<db::Param> params(items.begin(), items.end());
    return db::array_literal(params);
}

std::string format_double(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}

} // namespace

void verify_schema(db::Database& conn) {
    auto table = conn.exec("SELECT to_regclass('public.doc_chunks')::text");
    if (table.rows.empty() || table.rows[0].fields.at(0).empty()) {
        throw std::runtime_error("миграции не накатаны: нет таблицы doc_chunks — запустите migrate.sh <env> docs "
                                 "(wallet-infra) или migrations/000001_init.up.sql");
    }
    auto ext = conn.exec("SELECT 1 FROM pg_extension WHERE extname = 'vector'");
    if (ext.rows.empty()) {
        throw std::runtime_error("миграции не накатаны: нет расширения vector — его создаёт init-db.sh wallet-infra");
    }
}

std::map<std::string, std::string> PgStore::embeddings(const std::string& repo) {
    try {
        auto conn = pool_.acquire();
        auto r = conn->exec("SELECT path, heading, content_hash, embedding::text FROM doc_chunks "
                            "WHERE repo = $1 AND embedding IS NOT NULL",
                            {repo});
        std::map<std::string, std::string> out;
        for (const db::Row& row : r.rows) {
            out.emplace(store::embedding_key(row.fields.at(0), row.fields.at(1), row.fields.at(2)), row.fields.at(3));
        }
        return out;
    } catch (const std::exception& e) {
        throw store::Error(e.what());
    }
}

void PgStore::replace_repo(const std::string& repo, const std::vector<store::NewChunk>& chunks) {
    std::vector<db::Param> path, kind, capability, heading, content, hash, embedding;
    for (const store::NewChunk& c : chunks) {
        path.emplace_back(c.path);
        kind.emplace_back(c.kind);
        capability.emplace_back(c.capability);
        heading.emplace_back(c.heading);
        content.emplace_back(c.content);
        hash.emplace_back(c.content_hash);
        embedding.push_back(c.embedding.empty() ? db::Param{} : db::Param{c.embedding});
    }
    try {
        auto conn = pool_.acquire();
        auto tx = conn->begin();
        // Читатели видят либо старый индекс репозитория, либо новый (D6).
        tx->exec("DELETE FROM doc_chunks WHERE repo = $1", {repo});
        if (!chunks.empty()) {
            tx->exec(kInsertSql, {repo, db::array_literal(path), db::array_literal(kind),
                                  db::array_literal(capability), db::array_literal(heading),
                                  db::array_literal(content), db::array_literal(hash), db::array_literal(embedding)});
        }
        tx->commit();
    } catch (const std::exception& e) {
        throw store::Error(e.what());
    }
}

store::Candidates PgStore::search(const store::Scope& scope, const std::string& question,
                                  const std::string& query_vector, std::size_t limit, double max_distance) {
    store::Candidates out;
    std::string all = scope.all ? "true" : "false";
    std::string repos = text_array(scope.repos);
    std::string kinds = text_array(scope.kinds);
    std::string lim = std::to_string(limit);
    try {
        auto conn = pool_.acquire();
        out.text = to_hits(conn->exec(kTextSql, {question, all, repos, kinds, lim}));
        if (!query_vector.empty()) {
            out.vector =
                to_hits(conn->exec(kVectorSql, {query_vector, all, repos, kinds, format_double(max_distance), lim}));
        }
    } catch (const std::exception& e) {
        throw store::Error(e.what());
    }
    return out;
}

} // namespace pgstore
