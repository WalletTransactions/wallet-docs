// store::Store на Postgres (pgvector): таблица doc_chunks из migrations/.
// Схему сервис не создаёт (роль только DML), а проверяет на старте.
#pragma once

#include <memory>
#include <string>

#include "db.hpp"
#include "store.hpp"

namespace pgstore {

// Есть ли таблица doc_chunks и расширение vector; нет — std::runtime_error
// с текстом «миграции не накатаны: …».
void verify_schema(db::Database& conn);

class PgStore : public store::Store {
public:
    explicit PgStore(db::Pool& pool) : pool_(pool) {}

    std::map<std::string, std::string> embeddings(const std::string& repo) override;
    void replace_repo(const std::string& repo, const std::vector<store::NewChunk>& chunks) override;
    store::Candidates search(const store::Scope& scope, const std::string& question, const std::string& query_vector,
                             std::size_t limit, double max_distance) override;

private:
    db::Pool& pool_;
};

} // namespace pgstore
