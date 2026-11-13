#include "db.hpp"

#include <chrono>

#include <libpq-fe.h>

#include "nlohmann/json.hpp"
#include "util/log.hpp"

namespace db {

namespace {

// NOTICE Postgres (например «text-search query doesn't contain lexemes» на
// вопросе из одних стоп-слов) — не ошибка; libpq по умолчанию печатает его в
// stderr мимо JSON-журнала.
void ignore_notice(void*, const char*) {}

} // namespace

Database::Database(const std::string& conninfo) {
    conn_ = PQconnectdb(conninfo.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        while (!err.empty() && (err.back() == '\n' || err.back() == ' ')) err.pop_back();
        throw std::runtime_error(err);
    }
    PQsetNoticeProcessor(conn_, ignore_notice, nullptr);
}

Database::~Database() {
    if (conn_ != nullptr) PQfinish(conn_);
}

Result Database::exec(const std::string& sql, const std::vector<Param>& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    return exec_locked(sql, params);
}

Result Database::exec_locked(const std::string& sql, const std::vector<Param>& params) {
    // Соединение могло умереть (рестарт Postgres): PQreset переоткрывает его
    // с теми же параметрами — как в wallet-ai/wallet-api.
    if (PQstatus(conn_) != CONNECTION_OK) {
        PQreset(conn_);
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn_);
            applog::error("db reconnect failed", {{"event", "db_reconnect_failed"}, {"error", err}});
            throw QueryError("database unavailable: " + err, "");
        }
        PQsetNoticeProcessor(conn_, ignore_notice, nullptr);
        applog::info("db reconnected", {{"event", "db_reconnected"}});
    }

    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& p : params) values.push_back(p ? p->c_str() : nullptr);

    auto started = std::chrono::steady_clock::now();
    PGresult* res = PQexecParams(conn_, sql.c_str(), static_cast<int>(values.size()), nullptr, values.data(), nullptr,
                                 nullptr, 0);
    double duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    // В журнал — только SQL с плейсхолдерами: параметры — тексты спек и
    // векторы, им там не место.
    nlohmann::json details = {{"event", "db_query"}, {"sql", sql}, {"duration_ms", duration_ms}};
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string err = res != nullptr ? PQresultErrorMessage(res) : PQerrorMessage(conn_);
        const char* state = res != nullptr ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : nullptr;
        std::string sqlstate = state != nullptr ? state : "";
        PQclear(res);
        details["event"] = "db_query_failed";
        details["error"] = err;
        details["sqlstate"] = sqlstate;
        applog::error("db query failed", details);
        throw QueryError("Postgres: " + err, sqlstate);
    }

    Result out;
    int nrows = PQntuples(res);
    int ncols = PQnfields(res);
    out.rows.reserve(static_cast<std::size_t>(nrows));
    for (int r = 0; r < nrows; ++r) {
        Row row;
        row.fields.reserve(static_cast<std::size_t>(ncols));
        for (int c = 0; c < ncols; ++c) {
            row.fields.emplace_back(PQgetisnull(res, r, c) != 0 ? std::string{} : std::string(PQgetvalue(res, r, c)));
        }
        out.rows.push_back(std::move(row));
    }
    PQclear(res);
    details["rows"] = out.rows.size();
    applog::debug("db query", details);
    return out;
}

Database::Transaction::Transaction(Database& db) : db_(db), lock_(db.mutex_) { db_.exec_locked("BEGIN", {}); }

Database::Transaction::~Transaction() {
    if (committed_) return;
    try {
        db_.exec_locked("ROLLBACK", {});
    } catch (const std::exception& ex) {
        applog::error(std::string("ROLLBACK failed: ") + ex.what());
    }
}

Result Database::Transaction::exec(const std::string& sql, const std::vector<Param>& params) {
    return db_.exec_locked(sql, params);
}

void Database::Transaction::commit() {
    db_.exec_locked("COMMIT", {});
    committed_ = true;
}

std::unique_ptr<Database::Transaction> Database::begin() {
    return std::unique_ptr<Transaction>(new Transaction(*this));
}

Pool::Pool(const std::string& conninfo, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        conns_.push_back(std::make_unique<Database>(conninfo));
        idle_.push_back(conns_.back().get());
    }
}

Pool::Lease::~Lease() {
    if (conn_ != nullptr) pool_->release(conn_);
}

Pool::Lease Pool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !idle_.empty(); });
    Database* conn = idle_.back();
    idle_.pop_back();
    return Lease(this, conn);
}

void Pool::release(Database* conn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        idle_.push_back(conn);
    }
    cv_.notify_one();
}

std::string array_literal(const std::vector<Param>& items) {
    std::string out = "{";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out.push_back(',');
        if (!items[i]) {
            out += "NULL";
            continue;
        }
        out.push_back('"');
        for (char c : *items[i]) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        out.push_back('"');
    }
    out.push_back('}');
    return out;
}

} // namespace db
