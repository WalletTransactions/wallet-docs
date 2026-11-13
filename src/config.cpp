#include "config.hpp"

#include <cstdlib>
#include <stdexcept>

namespace config {

namespace {

std::string strip_trailing_slash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

std::string required_message(const std::string& key, const std::string& hint) {
    return key + " must be set — " + hint + " (см. README.md)";
}

// Целое в [lo, hi]; nullopt — не число или вне диапазона.
std::optional<int> parse_int(const std::string& s, int lo, int hi) {
    try {
        std::size_t used = 0;
        long v = std::stol(s, &used);
        if (used != s.size() || v < lo || v > hi) return std::nullopt;
        return static_cast<int>(v);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

Lookup process_env() {
    return [](const std::string& key) -> std::optional<std::string> {
        const char* v = std::getenv(key.c_str());
        if (v == nullptr || *v == '\0') return std::nullopt;
        return std::string(v);
    };
}

LoadResult load(const Lookup& env) {
    LoadResult out;
    Config c;

    auto db = env("DATABASE_URL");
    if (!db) {
        out.error = required_message("DATABASE_URL", "строка подключения libpq к базе docs, например "
                                                     "postgresql://docs:…@127.0.0.1:5432/wallet_docs");
        return out;
    }
    c.database_url = *db;

    auto auth = env("AUTH_SERVICE_URL");
    if (!auth) {
        out.error = required_message("AUTH_SERVICE_URL", "адрес google-auth для JWKS, например http://127.0.0.1:8080");
        return out;
    }
    c.auth_url = strip_trailing_slash(*auth);

    c.llm_url = strip_trailing_slash(env("LLM_URL").value_or(""));
    c.llm_model = env("LLM_MODEL").value_or("");
    c.llm_api_key = env("LLM_API_KEY").value_or("");
    c.embed_model = env("EMBED_MODEL").value_or("");
    c.index_token = env("INDEX_TOKEN").value_or("");
    c.host = env("HOST").value_or(c.host);

    struct IntVar {
        const char* key;
        int* target;
        int lo;
        int hi;
    };
    for (const IntVar& v : {IntVar{"PORT", &c.port, 1, 65535}, IntVar{"DB_POOL_SIZE", &c.db_pool_size, 1, 32},
                            IntVar{"LLM_TIMEOUT_MS", &c.llm_timeout_ms, 1000, 600000}}) {
        auto raw = env(v.key);
        if (!raw) continue;
        auto parsed = parse_int(*raw, v.lo, v.hi);
        if (!parsed) {
            out.error = std::string(v.key) + "=" + *raw + " — нужно целое от " + std::to_string(v.lo) + " до " +
                        std::to_string(v.hi) + " (см. README.md)";
            return out;
        }
        *v.target = *parsed;
    }

    out.config = c;
    return out;
}

} // namespace config
