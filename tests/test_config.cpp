#include <map>

#include "config.hpp"
#include "test_framework.hpp"

namespace {

config::Lookup env(std::map<std::string, std::string> vars) {
    return [vars](const std::string& key) -> std::optional<std::string> {
        auto it = vars.find(key);
        if (it == vars.end() || it->second.empty()) return std::nullopt;
        return it->second;
    };
}

} // namespace

TEST(config_requires_database_url) {
    auto r = config::load(env({{"AUTH_SERVICE_URL", "http://a"}}));
    CHECK(!r.config);
    CHECK(r.error.rfind("DATABASE_URL must be set — ", 0) == 0);
    CHECK(r.error.find("(см. README.md)") != std::string::npos);
}

TEST(config_requires_auth_url) {
    auto r = config::load(env({{"DATABASE_URL", "postgresql://x"}, {"AUTH_SERVICE_URL", ""}}));
    CHECK(!r.config);
    CHECK(r.error.rfind("AUTH_SERVICE_URL must be set — ", 0) == 0);
}

TEST(config_llm_optional) {
    auto r = config::load(env({{"DATABASE_URL", "postgresql://x"}, {"AUTH_SERVICE_URL", "http://auth:8080/"}}));
    REQUIRE(r.config);
    CHECK_EQ(r.config->auth_url, std::string("http://auth:8080"));
    CHECK(!r.config->chat_enabled());
    CHECK(!r.config->embeddings_enabled());
    CHECK(r.config->index_token.empty());
    CHECK_EQ(r.config->port, 8089);
}

TEST(config_full) {
    auto r = config::load(env({{"DATABASE_URL", "postgresql://x"},
                               {"AUTH_SERVICE_URL", "http://auth"},
                               {"LLM_URL", "http://lm:1234/v1/"},
                               {"LLM_MODEL", "chat"},
                               {"EMBED_MODEL", "nomic"},
                               {"INDEX_TOKEN", "t"},
                               {"PORT", "9000"}}));
    REQUIRE(r.config);
    CHECK_EQ(r.config->llm_url, std::string("http://lm:1234/v1"));
    CHECK(r.config->chat_enabled());
    CHECK(r.config->embeddings_enabled());
    CHECK_EQ(r.config->port, 9000);
}

// LLM_REASONING_CHAT: не задана и пустая строка — «none»; off — не передавать; остальное как есть.
TEST(config_reasoning_chat) {
    auto base = std::map<std::string, std::string>{{"DATABASE_URL", "postgresql://x"}, {"AUTH_SERVICE_URL", "http://a"}};
    auto with = [&](const std::string& value, bool set) {
        auto vars = base;
        if (set) vars["LLM_REASONING_CHAT"] = value;
        auto r = config::load(env(vars));
        REQUIRE(r.config);
        return r.config->llm_reasoning_chat;
    };
    CHECK_EQ(with("", false), std::string("none"));
    CHECK_EQ(with("", true), std::string("none"));
    CHECK_EQ(with("low", true), std::string("low"));
    CHECK_EQ(with("off", true), std::string(""));
}

TEST(config_bad_port) {
    auto r = config::load(env({{"DATABASE_URL", "postgresql://x"}, {"AUTH_SERVICE_URL", "http://a"}, {"PORT", "abc"}}));
    CHECK(!r.config);
    CHECK(r.error.rfind("PORT=abc", 0) == 0);
}
