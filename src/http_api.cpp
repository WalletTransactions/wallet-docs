#include "http_api.hpp"

#include <algorithm>

#include "answer.hpp"
#include "indexer.hpp"
#include "nlohmann/json.hpp"
#include "search.hpp"
#include "util/crypto.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"

namespace httpapi {

using nlohmann::json;

namespace {

constexpr const char* kJson = "application/json; charset=utf-8";

constexpr const char* kAuthMetric = "docs_auth_rejected_total";
constexpr const char* kAuthHelp = "Отказы в доступе к /api/docs/* по причине.";
constexpr const char* kAskMetric = "docs_answers_total";
constexpr const char* kAskHelp = "Вопросы POST /api/docs/ask по исходу.";
constexpr const char* kNoSourcesMetric = "docs_answers_without_sources_total";
constexpr const char* kNoSourcesHelp = "Ответы LLM без единой ссылки [n] на фрагмент (design D8, риск «модель не ставит ссылок»).";
constexpr const char* kRefusalMetric = "docs_refusals_total";
constexpr const char* kRefusalHelp =
    "Отказы «В описании системы этого нет.» по стадии: no_fragments — поиск ничего не нашёл (LLM не вызывалась), llm — отказ дала модель.";
constexpr const char* kIndexMetric = "docs_index_total";
constexpr const char* kIndexHelp = "Заливки PUT /docs/index/{repo} по исходу.";

void reply(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), kJson);
}

void reply_error(httplib::Response& res, int status, const std::string& error) {
    reply(res, status, json{{"error", error}});
}

// Проверка токена и области. false — ответ (401/403) уже записан.
bool authorize(const Deps& deps, const httplib::Request& req, httplib::Response& res, docsaccess::Identity& id,
               store::Scope& scope) {
    std::string token = docsaccess::request_token(req.get_header_value("Authorization"), req.get_header_value("Cookie"));
    id = docsaccess::verify(token, deps.keys);
    if (!id.ok) {
        metrics::inc_counter(kAuthMetric, kAuthHelp, "reason", id.reason);
        applog::info("docs access rejected", {{"event", "docs_auth_rejected"}, {"reason", id.reason}});
        reply_error(res, 401, "unauthorized");
        return false;
    }
    applog::set_user(id.sub);
    scope = docsaccess::scope_for(id.role, id.platform);
    if (scope.empty()) {
        metrics::inc_counter(kAuthMetric, kAuthHelp, "reason", "empty_scope");
        applog::info("docs scope is empty",
                     {{"event", "docs_forbidden"}, {"role", id.role}, {"platform", id.platform}});
        reply_error(res, 403, "forbidden");
        return false;
    }
    return true;
}

bool token_matches(const std::string& expected, const std::string& presented) {
    // Сравниваем SHA-256 за постоянное время: длина секрета тоже не утекает.
    return crypto::constant_time_equal(crypto::sha256(expected), crypto::sha256(presented));
}

std::string strip_nul(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    return s;
}

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void handle_status(const Deps& deps, const httplib::Request& req, httplib::Response& res) {
    applog::set_route("GET /api/docs/status");
    docsaccess::Identity id;
    store::Scope scope;
    if (!authorize(deps, req, res, id, scope)) return;
    // Ни LLM, ни база не проверяются (D10): статус — «раздел подключён и
    // доступен вам».
    reply(res, 200, json{{"enabled", true}});
}

void handle_ask(const Deps& deps, const httplib::Request& req, httplib::Response& res) {
    applog::set_route("POST /api/docs/ask");
    docsaccess::Identity id;
    store::Scope scope;
    if (!authorize(deps, req, res, id, scope)) return;

    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object() || !body.contains("question") || !body["question"].is_string()) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "bad_request");
        reply_error(res, 400, "question: нужна строка");
        return;
    }
    std::string question = trim(strip_nul(body["question"].get<std::string>()));
    if (question.empty()) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "bad_request");
        reply_error(res, 400, "question: пустой вопрос");
        return;
    }
    if (answer::utf8_length(question) > answer::kMaxQuestionChars) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "bad_request");
        reply_error(res, 400, "question: длиннее " + std::to_string(answer::kMaxQuestionChars) + " символов");
        return;
    }

    if (auto wait = deps.limiter->acquire(id.sub)) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "rate_limited");
        res.set_header("Retry-After", std::to_string(*wait));
        reply_error(res, 429, "too_many_requests");
        return;
    }

    if (deps.chat == nullptr) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "llm_unavailable");
        reply_error(res, 503, "llm_unavailable");
        return;
    }

    // Вектор вопроса — если эмбеддинги есть; иначе поиск только полнотекстом.
    std::string query_vector;
    if (deps.embedder != nullptr) {
        llm::EmbedResult e = deps.embedder->embed({std::string(llm::kQueryPrefix) + question});
        if (e.ok && e.vectors.size() == 1) {
            query_vector = search::vector_literal(e.vectors[0]);
        } else {
            applog::warn("question embedding failed, full-text only",
                         {{"event", "docs_query_embedding_failed"}, {"error", e.error}});
        }
    }

    std::vector<store::Hit> fragments;
    try {
        store::Candidates c =
            deps.store->search(scope, question, query_vector, search::kCandidates, search::kMaxVectorDistance);
        fragments = search::fuse(c);
        applog::info("docs search", {{"event", "docs_search"},
                                     {"text_candidates", c.text.size()},
                                     {"vector_candidates", c.vector.size()},
                                     {"with_vector", !query_vector.empty()},
                                     {"fragments", fragments.size()}});
    } catch (const store::Error& e) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "database_unavailable");
        applog::error("docs search failed", {{"event", "docs_search_failed"}, {"error", e.what()}});
        reply_error(res, 503, "database_unavailable");
        return;
    }

    if (fragments.empty()) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "not_found");
        metrics::inc_counter(kRefusalMetric, kRefusalHelp, "stage", "no_fragments");
        reply(res, 200, json{{"answer", answer::kNotFound}, {"sources", json::array()}});
        return;
    }

    llm::ChatResult r = deps.chat->complete(answer::system_prompt(), answer::user_prompt(question, fragments));
    if (!r.ok) {
        metrics::inc_counter(kAskMetric, kAskHelp, "result", "llm_unavailable");
        applog::warn("llm call failed", {{"event", "docs_llm_failed"}, {"error", r.error}});
        reply_error(res, 503, "llm_unavailable");
        return;
    }

    answer::Cited cited = answer::cite(r.content, fragments);
    if (cited.sources.empty()) {
        metrics::inc_counter(kNoSourcesMetric, kNoSourcesHelp, "reason", "no_citations");
    }
    metrics::inc_counter(kAskMetric, kAskHelp, "result", "answered");
    if (cited.sources.empty() && cited.answer.find(answer::kNotFound) != std::string::npos) {
        metrics::inc_counter(kRefusalMetric, kRefusalHelp, "stage", "llm");
    }
    json sources = json::array();
    for (const answer::Source& s : cited.sources) {
        sources.push_back({{"repo", s.repo}, {"capability", s.capability}, {"heading", s.heading}});
    }
    applog::info("docs answered", {{"event", "docs_answered"}, {"sources", cited.sources.size()}});
    reply(res, 200, json{{"answer", cited.answer}, {"sources", sources}});
}

void handle_index(const Deps& deps, const httplib::Request& req, httplib::Response& res) {
    applog::set_route("PUT /docs/index/:repo");
    if (deps.index_token.empty()) {
        metrics::inc_counter(kIndexMetric, kIndexHelp, "result", "disabled");
        reply_error(res, 503, "index_disabled");
        return;
    }
    if (!req.has_header("X-Index-Token") || !token_matches(deps.index_token, req.get_header_value("X-Index-Token"))) {
        metrics::inc_counter(kIndexMetric, kIndexHelp, "result", "unauthorized");
        reply_error(res, 401, "unauthorized");
        return;
    }
    std::string repo = req.path_params.count("repo") != 0 ? req.path_params.at("repo") : std::string{};
    if (!indexer::valid_repo_name(repo)) {
        metrics::inc_counter(kIndexMetric, kIndexHelp, "result", "bad_request");
        reply_error(res, 400, "repo: нужно ^[a-z0-9][a-z0-9-]{0,63}$");
        return;
    }

    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object() || !body.contains("files") || !body["files"].is_array()) {
        metrics::inc_counter(kIndexMetric, kIndexHelp, "result", "bad_request");
        reply_error(res, 400, "files: нужен массив {path, content}");
        return;
    }
    std::vector<indexer::File> files;
    for (const json& f : body["files"]) {
        if (!f.is_object() || !f.contains("path") || !f["path"].is_string() || !f.contains("content") ||
            !f["content"].is_string()) {
            metrics::inc_counter(kIndexMetric, kIndexHelp, "result", "bad_request");
            reply_error(res, 400, "files: каждый элемент — {path: строка, content: строка}");
            return;
        }
        files.push_back({f["path"].get<std::string>(), strip_nul(f["content"].get<std::string>())});
    }

    try {
        indexer::Stats s = indexer::index_repo(*deps.store, deps.embedder, repo, files);
        metrics::inc_counter(kIndexMetric, kIndexHelp, "result", "ok");
        json details = {{"event", "docs_indexed"}, {"repo", repo},           {"files", files.size()},
                        {"chunks", s.chunks},      {"embedded", s.embedded}, {"reused", s.reused},
                        {"skipped", s.skipped}};
        if (!s.embed_error.empty()) details["embed_error"] = s.embed_error;
        applog::info("docs index replaced", details);
        reply(res, 200,
              json{{"repo", repo},
                   {"chunks", s.chunks},
                   {"embedded", s.embedded},
                   {"reused", s.reused},
                   {"skipped", s.skipped}});
    } catch (const store::Error& e) {
        // Транзакция не зафиксирована — прежний индекс репозитория цел.
        metrics::inc_counter(kIndexMetric, kIndexHelp, "result", "failed");
        applog::error("docs index failed", {{"event", "docs_index_failed"}, {"repo", repo}, {"error", e.what()}});
        reply_error(res, 500, "index_failed");
    }
}

} // namespace

void register_routes(httplib::Server& svr, const Deps& deps) {
    svr.Get("/api/docs/status",
            [&deps](const httplib::Request& req, httplib::Response& res) { handle_status(deps, req, res); });
    svr.Post("/api/docs/ask",
             [&deps](const httplib::Request& req, httplib::Response& res) { handle_ask(deps, req, res); });
    svr.Put("/docs/index/:repo",
            [&deps](const httplib::Request& req, httplib::Response& res) { handle_index(deps, req, res); });
}

} // namespace httpapi
