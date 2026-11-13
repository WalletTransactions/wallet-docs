// Клиенты LM Studio (OpenAI-совместимый API): эмбеддинги (POST /embeddings,
// design D2) и ответ (POST /chat/completions, D8). Вызываются напрямую, не
// через wallet-ai (Non-Goals design: без связности между сервисами).
// Разбор тел — чистые функции; HTTP — за интерфейсами, чтобы тесты
// подставляли заглушки.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace llm {

// Префиксы задачи nomic-embed-text-v1.5 (D2): смена означает переиндексацию.
inline constexpr const char* kDocumentPrefix = "search_document: ";
inline constexpr const char* kQueryPrefix = "search_query: ";
// Размерность vector(768) в миграции.
inline constexpr std::size_t kEmbeddingDim = 768;

// "http://host:1234/v1/" -> {"http://host:1234", "/v1"} (как в wallet-ai).
struct BaseUrl {
    std::string origin;
    std::string path;
};
BaseUrl split_base_url(const std::string& url);

struct EmbedResult {
    bool ok = false;
    std::vector<std::vector<float>> vectors; // по одному на вход, в том же порядке
    std::string error;                       // для логов, без текстов
};

// Тело запроса: {"model", "input": [тексты как есть]} — префикс добавляет
// вызывающий код.
nlohmann::json embeddings_body(const std::string& model, const std::vector<std::string>& texts);
// Разбор ответа; ok только при 2xx, ровно expected векторах размерности
// kEmbeddingDim (порядок — по полю index, если оно есть).
EmbedResult parse_embeddings(int http_status, const std::string& body, std::size_t expected);

class Embedder {
public:
    virtual ~Embedder() = default;
    virtual EmbedResult embed(const std::vector<std::string>& texts) = 0;
};

struct ChatResult {
    bool ok = false;
    std::string content; // ответ модели без <think>…</think>
    std::string error;
};

nlohmann::json chat_body(const std::string& model, const std::string& system, const std::string& user);
ChatResult parse_chat(int http_status, const std::string& body);
// Вырезает рассуждения <think>…</think> и пробелы по краям.
std::string strip_think(const std::string& content);

class Chat {
public:
    virtual ~Chat() = default;
    virtual ChatResult complete(const std::string& system, const std::string& user) = 0;
};

struct HttpOptions {
    std::string url;     // LLM_URL
    std::string model;   // LLM_MODEL или EMBED_MODEL
    std::string api_key; // пусто — без Authorization
    int timeout_ms = 120000;
};

std::unique_ptr<Embedder> make_http_embedder(const HttpOptions& opts);
std::unique_ptr<Chat> make_http_chat(const HttpOptions& opts);

} // namespace llm
