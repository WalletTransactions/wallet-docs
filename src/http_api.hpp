// Ручки сервиса (openapi.yaml): GET /api/docs/status, POST /api/docs/ask
// (через гейтвей, JWT) и PUT /docs/index/{repo} (Jenkins по сети контура,
// X-Index-Token; гейтвей этот путь не проксирует, D7).
#pragma once

#include <cstddef>
#include <string>

#include "access.hpp"
#include "httplib.h"
#include "llm.hpp"
#include "rate_limit.hpp"
#include "store.hpp"

namespace httpapi {

// Лимит тела — заливка репозитория целиком (спека docs-indexing).
inline constexpr std::size_t kMaxBodyBytes = 16 * 1024 * 1024;
// Лимит вопросов на sub (D9).
inline constexpr std::size_t kQuestionsPerWindow = 6;
inline constexpr int kWindowSeconds = 60;

struct Deps {
    docsaccess::KeyLookup keys;
    store::Store* store = nullptr;     // обязателен
    llm::Embedder* embedder = nullptr; // nullptr — нет EMBED_MODEL: поиск только полнотекстом
    llm::Chat* chat = nullptr;         // nullptr — нет LLM_URL/LLM_MODEL: вопрос 503
    ratelimit::Limiter* limiter = nullptr;
    std::string index_token;           // пусто — заливка 503
};

// deps должен жить дольше сервера.
void register_routes(httplib::Server& svr, const Deps& deps);

} // namespace httpapi
