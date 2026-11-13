// Заливка индекса репозитория (design D6, спека docs-indexing): разбор
// путей, чанкинг, эмбеддинги (с повторным использованием по хэшу) до
// транзакции, затем замена чанков репозитория одной транзакцией.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "llm.hpp"
#include "store.hpp"

namespace indexer {

// Пачка текстов в одном POST /embeddings.
inline constexpr std::size_t kEmbedBatch = 16;
// Сколько байт текста чанка уходит в эмбеддинг: окно nomic в LM Studio —
// 2048 токенов, русский текст — ~2,5 символа на токен; хвост длинного чанка
// на вектор почти не влияет, а полный текст остаётся в content.
inline constexpr std::size_t kMaxEmbedChars = 4000;

// ^[a-z0-9][a-z0-9-]{0,63}$
bool valid_repo_name(const std::string& repo);

struct File {
    std::string path;
    std::string content;
};

struct Stats {
    std::size_t chunks = 0;
    std::size_t embedded = 0; // новые векторы, посчитанные в этой заливке
    std::size_t reused = 0;   // векторы из прежнего индекса
    std::size_t skipped = 0;  // файлы, не прошедшие classify_path
    std::string embed_error;  // почему эмбеддинги не посчитаны (для журнала)
};

// SHA-256 содержимого чанка, hex.
std::string content_hash(const std::string& content);

// Текст для эмбеддинга чанка: префикс search_document, заголовок, текст
// (обрезанный до kMaxEmbedChars по границе символа).
std::string embed_text(const std::string& heading, const std::string& content);

// embedder == nullptr — эмбеддинги выключены (нет EMBED_MODEL): чанки без
// векторов, кроме взятых из прежнего индекса. Первая неудачная пачка
// выключает эмбеддинги до конца заливки. Ошибки базы — store::Error.
Stats index_repo(store::Store& db, llm::Embedder* embedder, const std::string& repo, const std::vector<File>& files);

} // namespace indexer
