// Тонкая обёртка над libpq — тот же подход, что у wallet-ai (src/db.hpp):
// Database — одно соединение с переподключением; Pool — N соединений,
// раздаваемых на время одного вызова. NULL в результате — пустая строка.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
typedef struct pg_conn PGconn;
}

namespace pg_recover {
class Keepalive; // util/pg_recover.hpp; полный тип нужен только db.cpp
}

namespace db {

struct Row {
    std::vector<std::string> fields;
};

struct Result {
    std::vector<Row> rows;
};

// Ошибка SQL или соединения. sqlstate — 5 символов, пусто для ошибок
// соединения.
struct QueryError : std::runtime_error {
    std::string sqlstate;
    QueryError(const std::string& message, std::string state) : std::runtime_error(message), sqlstate(std::move(state)) {}
};

// Параметр запроса: nullopt — SQL NULL.
using Param = std::optional<std::string>;

class Database {
public:
    // Бросает std::runtime_error с причиной libpq, если соединение не
    // открылось.
    explicit Database(const std::string& conninfo);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    Result exec(const std::string& sql, const std::vector<Param>& params = {});

    // Транзакция держит соединение на всё время жизни; без commit() —
    // ROLLBACK в деструкторе.
    class Transaction {
    public:
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        Result exec(const std::string& sql, const std::vector<Param>& params = {});
        void commit();

    private:
        friend class Database;
        explicit Transaction(Database& db);

        Database& db_;
        std::unique_lock<std::mutex> lock_;
        bool committed_ = false;
    };

    std::unique_ptr<Transaction> begin();

private:
    Result exec_locked(const std::string& sql, const std::vector<Param>& params);

    PGconn* conn_ = nullptr;
    std::mutex mutex_;
    // true между успешным BEGIN и COMMIT/ROLLBACK: PQreset и повтор запроса
    // запрещены (pg_recover::exec_params). Читается/пишется под mutex_.
    bool in_transaction_ = false;
    // Фоновая проверка соединения раз в минуту (pg_recover::Keepalive).
    std::unique_ptr<pg_recover::Keepalive> keepalive_;
};

class Pool {
public:
    // Открывает size соединений; первое неудачное — std::runtime_error.
    Pool(const std::string& conninfo, std::size_t size);

    class Lease {
    public:
        Lease(Lease&& other) noexcept : pool_(other.pool_), conn_(other.conn_) { other.conn_ = nullptr; }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease& operator=(Lease&&) = delete;
        ~Lease();

        Database& operator*() const { return *conn_; }
        Database* operator->() const { return conn_; }

    private:
        friend class Pool;
        Lease(Pool* pool, Database* conn) : pool_(pool), conn_(conn) {}
        Pool* pool_;
        Database* conn_;
    };

    Lease acquire();

private:
    void release(Database* conn);

    std::vector<std::unique_ptr<Database>> conns_;
    std::vector<Database*> idle_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// Литерал массива Postgres для text[]: {"a","b",NULL}. nullopt — NULL.
std::string array_literal(const std::vector<Param>& items);

} // namespace db
