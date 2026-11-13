// wallet-docs — справочный сервис стека (openspec/changes/docs-service):
// отвечает на вопросы «как это работает» по OpenSpec-спекам репозиториев.
// Индекс (doc_chunks, pgvector) заливает Jenkins через PUT /docs/index/{repo};
// пользователи спрашивают через гейтвей (/api/docs/*), область документации
// определяет сервер по role и platform проверенного JWT.
//
// Необязательный модуль контура: от сервиса никто не зависит, а он сам — только
// от Postgres, google-auth (JWKS) и LM Studio.

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

#include "access.hpp"
#include "config.hpp"
#include "db.hpp"
#include "http_api.hpp"
#include "httplib.h"
#include "llm.hpp"
#include "nlohmann/json.hpp"
#include "pg_store.hpp"
#include "rate_limit.hpp"
#include "service_endpoints.hpp"
#include "util/dotenv.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"
#include "version.hpp"

namespace {

// trace_id ставит гейтвей — сам сервис новый не порождает.
httplib::Server::HandlerResponse handle_pre_routing(const httplib::Request& req, httplib::Response& res) {
    applog::start_request_timer();
    std::string trace_id = req.get_header_value("X-Trace-Id");
    if (!trace_id.empty()) {
        applog::set_trace_id(trace_id);
        res.set_header("X-Trace-Id", trace_id);
    }
    return httplib::Server::HandlerResponse::Unhandled;
}

} // namespace

int main() {
    dotenv::load(".env");

    const char* log_dir_env = std::getenv("LOG_DIR");
    std::string log_dir = log_dir_env != nullptr ? log_dir_env : "";
    if (log_dir == "stdout" || log_dir == "-") log_dir.clear();
    applog::init(log_dir, "wallet-docs");
    metrics::set_build_info(version::kService, version::kVersion, version::kBuild, version::kGitCommit);

    config::LoadResult loaded = config::load(config::process_env());
    if (!loaded.config) {
        applog::error(loaded.error);
        return 1;
    }
    const config::Config& cfg = *loaded.config;

    // База обязательна на старте: без неё сервис не поднимается, перезапуск —
    // политика restart compose (спека docs-service-ops, «Недоступная база»).
    std::unique_ptr<db::Pool> pool;
    try {
        pool = std::make_unique<db::Pool>(cfg.database_url, static_cast<std::size_t>(cfg.db_pool_size));
    } catch (const std::exception& e) {
        applog::error(std::string("database unavailable: ") + e.what(), {{"event", "db_unavailable"}});
        return 1;
    }
    try {
        auto conn = pool->acquire();
        pgstore::verify_schema(*conn);
    } catch (const db::QueryError& e) {
        applog::error(std::string("database unavailable: ") + e.what(), {{"event", "db_unavailable"}});
        return 1;
    } catch (const std::exception& e) {
        applog::error(e.what(), {{"event", "schema_missing"}});
        return 1;
    }
    pgstore::PgStore store(*pool);

    std::unique_ptr<llm::Embedder> embedder;
    if (cfg.embeddings_enabled()) {
        embedder = llm::make_http_embedder({cfg.llm_url, cfg.embed_model, cfg.llm_api_key, cfg.llm_timeout_ms});
    } else {
        applog::warn("embeddings disabled (LLM_URL or EMBED_MODEL empty): search is full-text only");
    }
    std::unique_ptr<llm::Chat> chat;
    if (cfg.chat_enabled()) {
        chat = llm::make_http_chat(
            {cfg.llm_url, cfg.llm_model, cfg.llm_api_key, cfg.llm_timeout_ms, cfg.llm_reasoning_chat});
    } else {
        applog::warn("answers disabled (LLM_URL or LLM_MODEL empty): POST /api/docs/ask -> 503");
    }
    if (cfg.index_token.empty()) applog::warn("index upload disabled (INDEX_TOKEN empty): PUT /docs/index -> 503");

    ratelimit::Limiter limiter(httpapi::kQuestionsPerWindow, std::chrono::seconds(httpapi::kWindowSeconds));

    httpapi::Deps deps;
    deps.keys = docsaccess::lazy_jwks(cfg.auth_url);
    deps.store = &store;
    deps.embedder = embedder.get();
    deps.chat = chat.get();
    deps.limiter = &limiter;
    deps.index_token = cfg.index_token;

    httplib::Server svr;
    svr.set_pre_routing_handler(handle_pre_routing);
    svr.set_payload_max_length(httpapi::kMaxBodyBytes);
    httpapi::register_routes(svr, deps);
    endpoints::register_routes(svr);

    svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        std::string message = "internal error";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            message = e.what();
        }
        applog::error("unhandled exception: " + req.method + " " + req.path + " -> " + message);
        res.status = 500;
        res.set_content(nlohmann::json{{"error", "internal server error"}}.dump(), "application/json");
    });

    // Access-лог без тел: в них вопросы пользователей и тексты спек.
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        metrics::observe_http_request(req.method,
                                      applog::current_route().empty() ? "(unset)" : applog::current_route(),
                                      res.status, applog::request_duration_ms());
        if (req.path == "/healthz" || req.path == "/metrics") {
            applog::clear_request_context();
            return;
        }
        applog::info(req.method + " " + req.path + " -> " + std::to_string(res.status),
                     {{"event", "request_finished"}, {"duration_ms", applog::request_duration_ms()}});
        applog::clear_request_context();
    });

    applog::info("wallet-docs v" + std::string(version::kBuild) + " (" + std::string(version::kGitCommit) +
                 ") listening on http://" + cfg.host + ":" + std::to_string(cfg.port) +
                 " (google-auth: " + cfg.auth_url + ", llm: " + (cfg.llm_url.empty() ? "off" : cfg.llm_url) +
                 ", chat model: " + (cfg.llm_model.empty() ? "-" : cfg.llm_model) +
                 ", reasoning: " + (cfg.llm_reasoning_chat.empty() ? "-" : cfg.llm_reasoning_chat) +
                 ", embed model: " + (cfg.embed_model.empty() ? "-" : cfg.embed_model) +
                 ", index upload: " + (cfg.index_token.empty() ? "off" : "on") + ")");
    if (!svr.listen(cfg.host, cfg.port)) {
        applog::error("не удалось запустить сервер на " + cfg.host + ":" + std::to_string(cfg.port) +
                      " (порт занят или недоступен?)");
        return 1;
    }
    return 0;
}
