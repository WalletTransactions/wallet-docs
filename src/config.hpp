// Конфигурация сервиса (спека docs-service-ops, «Конфигурация»): обязательны
// DATABASE_URL и AUTH_SERVICE_URL; LLM_URL, LLM_MODEL, EMBED_MODEL и
// INDEX_TOKEN — нет: без них соответствующая часть выключена, а сервис
// стартует.
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace config {

struct Config {
    std::string database_url;
    std::string auth_url;      // без завершающего '/'
    std::string llm_url;       // OpenAI-совместимый корень, например http://host:1234/v1
    std::string llm_model;     // чат-модель ответа
    std::string llm_api_key;   // пусто — заголовок Authorization не передаётся
    std::string embed_model;   // nomic-embed-text-v1.5
    std::string index_token;   // пусто — PUT /docs/index отвечает 503
    std::string host = "127.0.0.1";
    int port = 8089;
    int db_pool_size = 4;
    int llm_timeout_ms = 120000;

    bool chat_enabled() const { return !llm_url.empty() && !llm_model.empty(); }
    bool embeddings_enabled() const { return !llm_url.empty() && !embed_model.empty(); }
};

using Lookup = std::function<std::optional<std::string>(const std::string& key)>;

// Окружение процесса; пустое значение считается незаданным.
Lookup process_env();

struct LoadResult {
    std::optional<Config> config;
    // При отказе — `<KEY> must be set — <подсказка> (см. README.md)` или
    // текст про неверное число.
    std::string error;
};

// Чистая функция: тесты передают свой Lookup.
LoadResult load(const Lookup& env);

} // namespace config
