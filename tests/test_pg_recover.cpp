// Общий код переподключения (wallet-common util/pg_recover) без Postgres:
// разбор «только чтение» и фоновый поток. Сами обрывы соединения проверяются
// на живой базе — wallet-api/tests_integration/test_db_reconnect.cpp.
#include <atomic>
#include <chrono>
#include <thread>

#include "test_framework.hpp"
#include "util/pg_recover.hpp"

TEST(pg_recover_select_and_with_are_read_only) {
    CHECK(pg_recover::is_read_only("SELECT 1"));
    CHECK(pg_recover::is_read_only("  select * from doc_chunks where id = $1"));
    CHECK(pg_recover::is_read_only("WITH q AS (SELECT 1) SELECT * FROM q"));
    // Колонка updated_at — не слово UPDATE.
    CHECK(pg_recover::is_read_only("SELECT updated_at FROM doc_chunks"));
}

TEST(pg_recover_writes_are_never_retried) {
    CHECK(!pg_recover::is_read_only("INSERT INTO t VALUES (1)"));
    CHECK(!pg_recover::is_read_only("UPDATE t SET v = 1"));
    CHECK(!pg_recover::is_read_only("DELETE FROM t"));
    CHECK(!pg_recover::is_read_only("WITH d AS (DELETE FROM t RETURNING *) SELECT * FROM d"));
    CHECK(!pg_recover::is_read_only("SELECT * FROM t FOR UPDATE"));
    CHECK(!pg_recover::is_read_only("SELECT nextval('s')"));
    CHECK(!pg_recover::is_read_only("BEGIN"));
    CHECK(!pg_recover::is_read_only("COMMIT"));
    CHECK(!pg_recover::is_read_only(""));
}

TEST(pg_recover_keepalive_ticks_and_stops_promptly) {
    std::atomic<int> ticks{0};
    {
        pg_recover::Keepalive ka([&ticks] { ++ticks; }, std::chrono::seconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    }
    CHECK_EQ(ticks.load(), 2);

    // Остановка не ждёт полного интервала (60 с): иначе выключение сервиса
    // зависало бы на минуту на каждое соединение пула.
    auto t0 = std::chrono::steady_clock::now();
    { pg_recover::Keepalive slow([] {}, std::chrono::seconds(60)); }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 1000);
}
