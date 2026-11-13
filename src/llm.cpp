#include "llm.hpp"

#include <algorithm>
#include <optional>

#include "httplib.h"

namespace llm {

using nlohmann::json;

BaseUrl split_base_url(const std::string& url) {
    std::string u = url;
    while (!u.empty() && u.back() == '/') u.pop_back();
    auto scheme_end = u.find("://");
    if (scheme_end == std::string::npos || scheme_end + 3 >= u.size()) return {};
    auto path_start = u.find('/', scheme_end + 3);
    if (path_start == std::string::npos) return {u, ""};
    return {u.substr(0, path_start), u.substr(path_start)};
}

json embeddings_body(const std::string& model, const std::vector<std::string>& texts) {
    return json{{"model", model}, {"input", texts}};
}

EmbedResult parse_embeddings(int http_status, const std::string& body, std::size_t expected) {
    EmbedResult r;
    if (http_status < 200 || http_status >= 300) {
        r.error = "http_" + std::to_string(http_status);
        return r;
    }
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("data") || !j["data"].is_array()) {
        r.error = "bad_response";
        return r;
    }
    const json& data = j["data"];
    if (data.size() != expected) {
        r.error = "count_mismatch";
        return r;
    }
    r.vectors.assign(expected, {});
    for (std::size_t i = 0; i < data.size(); ++i) {
        const json& item = data[i];
        std::size_t idx = i;
        if (item.contains("index") && item["index"].is_number_unsigned()) idx = item["index"].get<std::size_t>();
        if (idx >= expected || !item.contains("embedding") || !item["embedding"].is_array()) {
            r.error = "bad_response";
            r.vectors.clear();
            return r;
        }
        const json& emb = item["embedding"];
        if (emb.size() != kEmbeddingDim) {
            r.error = "dimension_" + std::to_string(emb.size());
            r.vectors.clear();
            return r;
        }
        std::vector<float> v;
        v.reserve(emb.size());
        for (const json& x : emb) {
            if (!x.is_number()) {
                r.error = "bad_response";
                r.vectors.clear();
                return r;
            }
            v.push_back(x.get<float>());
        }
        r.vectors[idx] = std::move(v);
    }
    for (const auto& v : r.vectors) {
        if (v.empty()) {
            r.error = "bad_response";
            r.vectors.clear();
            return r;
        }
    }
    r.ok = true;
    return r;
}

json chat_body(const std::string& model, const std::string& system, const std::string& user,
               const std::string& reasoning_effort) {
    json body{{"model", model},
              {"messages", json::array({{{"role", "system"}, {"content", system}},
                                        {{"role", "user"}, {"content", user}}})},
              // Ответ по фрагментам, а не творчество.
              {"temperature", 0.2},
              {"max_tokens", 1024},
              {"stream", false}};
    // Модели с рассуждением (gemma-4 в LM Studio) без этого тратят max_tokens на «мысли»:
    // content пуст, ответ идёт десятки секунд.
    if (!reasoning_effort.empty()) body["reasoning_effort"] = reasoning_effort;
    return body;
}

std::string strip_think(const std::string& content) {
    std::string s = content;
    auto end = s.find("</think>");
    if (s.find("<think>") != std::string::npos && end != std::string::npos) s = s.substr(end + 8);
    auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

ChatResult parse_chat(int http_status, const std::string& body) {
    ChatResult r;
    if (http_status < 200 || http_status >= 300) {
        r.error = "http_" + std::to_string(http_status);
        return r;
    }
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
        r.error = "bad_response";
        return r;
    }
    const json& msg = j["choices"][0].value("message", json::object());
    if (!msg.contains("content") || !msg["content"].is_string()) {
        r.error = "bad_response";
        return r;
    }
    r.content = strip_think(msg["content"].get<std::string>());
    if (r.content.empty()) {
        r.error = "empty_answer";
        return r;
    }
    r.ok = true;
    return r;
}

namespace {

// nullopt — до HTTP-ответа не дошло (сеть, таймаут); error заполнен.
std::optional<httplib::Response> post(const HttpOptions& opts, const BaseUrl& base, const std::string& path,
                                      const json& body, std::string& error) {
    if (base.origin.empty()) {
        error = "llm_disabled";
        return std::nullopt;
    }
    // Клиент на вызов: вызовы редкие и долгие, держать соединение незачем.
    httplib::Client cli(base.origin);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(opts.timeout_ms / 1000, (opts.timeout_ms % 1000) * 1000);
    cli.set_max_timeout(opts.timeout_ms);
    httplib::Headers headers;
    if (!opts.api_key.empty()) headers.emplace("Authorization", "Bearer " + opts.api_key);
    auto res = cli.Post(base.path + path, headers, body.dump(), "application/json");
    if (!res) {
        error = httplib::to_string(res.error());
        return std::nullopt;
    }
    return *res;
}

class HttpEmbedder : public Embedder {
public:
    explicit HttpEmbedder(HttpOptions opts) : opts_(std::move(opts)), base_(split_base_url(opts_.url)) {}

    EmbedResult embed(const std::vector<std::string>& texts) override {
        if (texts.empty()) return EmbedResult{true, {}, {}};
        std::string error;
        auto res = post(opts_, base_, "/embeddings", embeddings_body(opts_.model, texts), error);
        if (!res) {
            EmbedResult r;
            r.error = error;
            return r;
        }
        return parse_embeddings(res->status, res->body, texts.size());
    }

private:
    HttpOptions opts_;
    BaseUrl base_;
};

class HttpChat : public Chat {
public:
    explicit HttpChat(HttpOptions opts) : opts_(std::move(opts)), base_(split_base_url(opts_.url)) {}

    ChatResult complete(const std::string& system, const std::string& user) override {
        std::string error;
        auto res = post(opts_, base_, "/chat/completions",
                         chat_body(opts_.model, system, user, opts_.reasoning_effort), error);
        if (!res) {
            ChatResult r;
            r.error = error;
            return r;
        }
        return parse_chat(res->status, res->body);
    }

private:
    HttpOptions opts_;
    BaseUrl base_;
};

} // namespace

std::unique_ptr<Embedder> make_http_embedder(const HttpOptions& opts) { return std::make_unique<HttpEmbedder>(opts); }

std::unique_ptr<Chat> make_http_chat(const HttpOptions& opts) { return std::make_unique<HttpChat>(opts); }

} // namespace llm
