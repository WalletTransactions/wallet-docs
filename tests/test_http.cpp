// Ручки на локальном httplib-сервере с фейковыми хранилищем, LLM и JWKS.
#include <memory>
#include <thread>

#include "fakes.hpp"
#include "http_api.hpp"
#include "httplib.h"
#include "test_framework.hpp"
#include "test_keys.hpp"
#include "util/metrics.hpp"

namespace {

using nlohmann::json;

struct Harness {
    fakes::MemStore store;
    fakes::FakeEmbedder embedder;
    fakes::FakeChat chat;
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
    ratelimit::Limiter limiter{6, std::chrono::seconds(60), [this] { return now; }};
    httpapi::Deps deps;
    httplib::Server svr;
    std::thread thread;
    int port = 0;

    explicit Harness(bool with_llm = true, std::string index_token = "secret-token") {
        deps.keys = testkeys::jwks_lookup();
        deps.store = &store;
        deps.embedder = with_llm ? &embedder : nullptr;
        deps.chat = with_llm ? &chat : nullptr;
        deps.limiter = &limiter;
        deps.index_token = std::move(index_token);
        svr.set_payload_max_length(httpapi::kMaxBodyBytes);
        httpapi::register_routes(svr, deps);
        port = svr.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { svr.listen_after_bind(); });
        svr.wait_until_ready();
    }
    ~Harness() {
        svr.stop();
        thread.join();
    }

    httplib::Client client() const {
        httplib::Client c("127.0.0.1", port);
        c.set_read_timeout(10, 0);
        return c;
    }

    httplib::Result status(const std::string& token) {
        return client().Get("/api/docs/status", httplib::Headers{{"Authorization", "Bearer " + token}});
    }
    httplib::Result ask(const std::string& token, const std::string& body) {
        return client().Post("/api/docs/ask", httplib::Headers{{"Authorization", "Bearer " + token}}, body, "application/json");
    }
    httplib::Result ask_q(const std::string& token, const std::string& question) {
        return ask(token, json{{"question", question}}.dump());
    }
    httplib::Result index(const std::string& repo, const json& body, const std::string& token = "secret-token") {
        httplib::Headers h;
        if (!token.empty()) h.emplace("X-Index-Token", token);
        return client().Put("/docs/index/" + repo, h, body.dump(), "application/json");
    }

    void seed() {
        auto spec = [](const std::string& cap, const std::string& text) {
            return json{{"path", "openspec/specs/" + cap + "/spec.md"},
                        {"content", "# s\n\n## Purpose\n\n" + text + "\n"}};
        };
        REQUIRE(index("android-wallet", {{"files", {spec("sync-run", "синхронизация телефона")}}})->status == 200);
        REQUIRE(index("wallet-gateway", {{"files", {spec("request-routing", "синхронизация гейтвея")}}})->status == 200);
        REQUIRE(index("wallet-web", {{"files", {spec("ui", "синхронизация браузера")}}})->status == 200);
    }
};

std::string android() { return testkeys::token("user", "android"); }
std::string admin() { return testkeys::token("admin", "web"); }

} // namespace

TEST(status_by_token) {
    Harness h;
    auto ok = h.status(android());
    REQUIRE(ok);
    CHECK_EQ(ok->status, 200);
    CHECK_EQ(json::parse(ok->body), json({{"enabled", true}}));
    CHECK_EQ(h.status(admin())->status, 200);
    CHECK_EQ(h.status(testkeys::token("user", "web"))->status, 200);
    auto other = h.status(testkeys::token("user", "other"));
    CHECK_EQ(other->status, 403);
    CHECK_EQ(json::parse(other->body), json({{"error", "forbidden"}}));
    CHECK_EQ(h.status("bad")->status, 401);
    CHECK_EQ(h.client().Get("/api/docs/status")->status, 401);
}

TEST(status_mcp_token_without_role) {
    Harness h;
    auto claims = testkeys::user_claims("user", "mcp");
    claims.erase("role");
    auto ok = h.status(testkeys::google_auth().sign(claims));
    REQUIRE(ok);
    CHECK_EQ(ok->status, 200);

    auto no_scope = claims;
    no_scope["scope"] = "reports:read";
    CHECK_EQ(h.status(testkeys::google_auth().sign(no_scope))->status, 401);
}

TEST(status_with_session_cookie) {
    Harness h;
    auto r = h.client().Get("/api/docs/status", httplib::Headers{{"Cookie", "session=" + android()}});
    CHECK_EQ(r->status, 200);
}

TEST(status_independent_of_llm_and_db) {
    Harness h(false);
    h.store.fail_search = true;
    CHECK_EQ(h.status(android())->status, 200);
}

TEST(ask_validation) {
    Harness h;
    CHECK_EQ(h.ask_q(android(), "")->status, 400);
    CHECK_EQ(h.ask_q(android(), "   ")->status, 400);
    std::string long_q;
    for (int i = 0; i < 1001; ++i) long_q += "ы";
    CHECK_EQ(h.ask_q(android(), long_q)->status, 400);
    std::string max_q;
    for (int i = 0; i < 1000; ++i) max_q += "ы";
    CHECK_EQ(h.ask_q(android(), max_q)->status, 200);
    CHECK_EQ(h.ask(android(), "not json")->status, 400);
    CHECK_EQ(h.ask(android(), R"({"question": 5})")->status, 400);
}

TEST(ask_forbidden_without_llm_call) {
    Harness h;
    h.seed();
    auto r = h.ask_q(testkeys::token("user", "other"), "синхронизация");
    CHECK_EQ(r->status, 403);
    CHECK_EQ(h.chat.calls, 0);
    CHECK_EQ(h.store.searches, 0);
}

TEST(ask_android_sees_only_android_specs) {
    Harness h;
    h.seed();
    h.chat.reply = "Синхронизация описана [1].";
    auto r = h.ask_q(android(), "синхронизация");
    REQUIRE(r->status == 200);
    auto body = json::parse(r->body);
    CHECK_EQ(body["answer"], "Синхронизация описана.");
    REQUIRE(body["sources"].size() == 1);
    CHECK_EQ(body["sources"][0]["repo"], "android-wallet");
    CHECK_EQ(body["sources"][0]["capability"], "sync-run");
    CHECK_EQ(body["sources"][0]["heading"], "Purpose");
    CHECK(h.chat.last_user.find("гейтвея") == std::string::npos);
    CHECK(h.chat.last_user.find("браузера") == std::string::npos);
    // Вопрос эмбеддится с префиксом search_query.
    CHECK_EQ(h.embedder.inputs.back(), std::string("search_query: синхронизация"));
    CHECK(!h.store.last_vector.empty());
}

TEST(ask_admin_sees_all) {
    Harness h;
    h.seed();
    h.chat.reply = "Везде [1] [2] [3].";
    auto r = h.ask_q(admin(), "синхронизация");
    REQUIRE(r->status == 200);
    CHECK_EQ(json::parse(r->body)["sources"].size(), 3u);
}

TEST(ask_not_found_skips_llm) {
    Harness h;
    h.seed();
    auto r = h.ask_q(android(), "Какая погода в Москве?");
    REQUIRE(r->status == 200);
    CHECK_EQ(json::parse(r->body), json({{"answer", "В описании системы этого нет."}, {"sources", json::array()}}));
    CHECK_EQ(h.chat.calls, 0);
}

namespace {
// Счётчики глобальны на процесс — сравниваем приращение.
long refusals(const std::string& stage) {
    std::string text = metrics::render();
    std::string key = "docs_refusals_total{stage=\"" + stage + "\"} ";
    std::size_t p = text.find(key);
    return p == std::string::npos ? 0 : std::stol(text.substr(p + key.size()));
}
} // namespace

TEST(refusal_metric_by_stage) {
    Harness h;
    h.seed();
    long before_none = refusals("no_fragments");
    long before_llm = refusals("llm");

    CHECK_EQ(h.ask_q(android(), "Какая погода в Москве?")->status, 200);
    CHECK_EQ(refusals("no_fragments"), before_none + 1);
    CHECK_EQ(refusals("llm"), before_llm);

    h.chat.reply = "В описании системы этого нет.";
    CHECK_EQ(h.ask_q(android(), "синхронизация")->status, 200);
    CHECK_EQ(refusals("llm"), before_llm + 1);
    CHECK_EQ(refusals("no_fragments"), before_none + 1);

    h.chat.reply = "Синхронизация описана [1].";
    CHECK_EQ(h.ask_q(android(), "синхронизация")->status, 200);
    CHECK_EQ(refusals("llm"), before_llm + 1);
}

TEST(ask_without_citations) {
    Harness h;
    h.seed();
    h.chat.reply = "Ответ без ссылок.";
    auto r = h.ask_q(android(), "синхронизация");
    REQUIRE(r->status == 200);
    auto body = json::parse(r->body);
    CHECK_EQ(body["answer"], "Ответ без ссылок.");
    CHECK(body["sources"].empty());
}

TEST(ask_llm_down_503_status_stays_200) {
    Harness h;
    h.seed();
    h.chat.fail = true;
    auto r = h.ask_q(android(), "синхронизация");
    CHECK_EQ(r->status, 503);
    CHECK_EQ(json::parse(r->body), json({{"error", "llm_unavailable"}}));
    CHECK_EQ(h.status(android())->status, 200);
}

TEST(ask_without_llm_config_503) {
    Harness h(false);
    auto r = h.ask_q(android(), "синхронизация");
    CHECK_EQ(r->status, 503);
    CHECK_EQ(json::parse(r->body), json({{"error", "llm_unavailable"}}));
}

TEST(ask_embeddings_down_full_text_only) {
    Harness h;
    h.seed();
    h.embedder.fail = true;
    auto r = h.ask_q(android(), "синхронизация");
    CHECK_EQ(r->status, 200);
    CHECK(h.store.last_vector.empty());
}

TEST(ask_database_down_503) {
    Harness h;
    h.store.fail_search = true;
    auto r = h.ask_q(android(), "синхронизация");
    CHECK_EQ(r->status, 503);
    CHECK_EQ(json::parse(r->body), json({{"error", "database_unavailable"}}));
}

TEST(ask_rate_limit_seventh_429) {
    Harness h;
    for (int i = 0; i < 6; ++i) CHECK_EQ(h.ask_q(android(), "вопрос")->status, 200);
    auto r = h.ask_q(android(), "вопрос");
    CHECK_EQ(r->status, 429);
    CHECK_EQ(json::parse(r->body), json({{"error", "too_many_requests"}}));
    CHECK_EQ(r->get_header_value("Retry-After"), std::string("60"));
    // Статус и заливка лимитом не затронуты.
    CHECK_EQ(h.status(android())->status, 200);
    CHECK_EQ(h.index("x", {{"files", json::array()}})->status, 200);
}

TEST(index_auth_and_validation) {
    Harness h;
    json body = {{"files", json::array()}};
    CHECK_EQ(h.index("android-wallet", body, "")->status, 401);
    CHECK_EQ(h.index("android-wallet", body, "wrong")->status, 401);
    CHECK_EQ(h.index("Android%20Wallet", body)->status, 400);
    CHECK_EQ(h.index("wallet_web", body)->status, 400);
    CHECK_EQ(h.index("android-wallet", {{"nope", 1}})->status, 400);
    CHECK_EQ(h.index("android-wallet", {{"files", {{{"path", 1}}}}})->status, 400);
    CHECK(h.store.rows.empty());
}

TEST(index_disabled_without_token) {
    Harness h(true, "");
    auto r = h.index("android-wallet", {{"files", json::array()}}, "anything");
    CHECK_EQ(r->status, 503);
    // Вопросы по имеющемуся индексу работают.
    CHECK_EQ(h.status(android())->status, 200);
}

TEST(index_response_counts) {
    Harness h;
    json files = {{{"path", "openspec/specs/a/spec.md"}, {"content", "## Purpose\n\nP.\n\n### Requirement: R\nT.\n"}},
                  {{"path", "openspec/project.md"}, {"content", "x"}},
                  {{"path", "../etc/passwd"}, {"content", "x"}}};
    auto r = h.index("android-wallet", {{"files", files}});
    REQUIRE(r->status == 200);
    CHECK_EQ(json::parse(r->body),
             json({{"repo", "android-wallet"}, {"chunks", 2}, {"embedded", 2}, {"reused", 0}, {"skipped", 2}}));
    auto again = h.index("android-wallet", {{"files", files}});
    CHECK_EQ(json::parse(again->body)["reused"], 2);
}

TEST(index_db_failure_500) {
    Harness h;
    h.store.fail_replace = true;
    auto r = h.index("android-wallet",
                     {{"files", {{{"path", "openspec/specs/a/spec.md"}, {"content", "## Purpose\n\nP.\n"}}}}});
    CHECK_EQ(r->status, 500);
}

TEST(index_body_too_large_413) {
    Harness h;
    std::string big(httpapi::kMaxBodyBytes + 10, 'x');
    auto r = h.client().Put("/docs/index/a", httplib::Headers{{"X-Index-Token", "secret-token"}}, big, "application/json");
    REQUIRE(r);
    CHECK_EQ(r->status, 413);
}
