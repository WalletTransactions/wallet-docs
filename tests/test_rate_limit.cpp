#include "rate_limit.hpp"
#include "test_framework.hpp"

TEST(rate_limit_seventh_question_waits) {
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    ratelimit::Limiter l(6, std::chrono::seconds(60), [&now] { return now; });
    for (int i = 0; i < 6; ++i) {
        CHECK(!l.acquire("u1"));
        now += std::chrono::seconds(1);
    }
    // Первый вопрос был 6 с назад: слот освободится через 54 с.
    auto wait = l.acquire("u1");
    REQUIRE(wait);
    CHECK_EQ(*wait, 54);
    // Другой пользователь не затронут.
    CHECK(!l.acquire("u2"));
    // Отказ не занимает слот: через 54 с вопрос проходит.
    now += std::chrono::seconds(54);
    CHECK(!l.acquire("u1"));
    CHECK(l.acquire("u1"));
}

TEST(rate_limit_retry_after_rounds_up) {
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    ratelimit::Limiter l(1, std::chrono::seconds(60), [&now] { return now; });
    CHECK(!l.acquire("u"));
    now += std::chrono::milliseconds(59500);
    auto wait = l.acquire("u");
    REQUIRE(wait);
    CHECK_EQ(*wait, 1);
}
