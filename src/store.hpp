// Хранилище индекса — интерфейс над таблицей doc_chunks. Реализация на
// Postgres — pg_store.hpp; юнит-тесты подставляют свою в памяти.
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace store {

// Область документации пользователя (design D5). all — без фильтра;
// иначе repos × kinds. Пустая область сюда не доходит: её отсекает HTTP-слой
// ответом 403.
struct Scope {
    bool all = false;
    std::vector<std::string> repos;
    std::vector<std::string> kinds;

    bool empty() const { return !all && (repos.empty() || kinds.empty()); }
};

struct NewChunk {
    std::string repo;
    std::string path;
    std::string kind;
    std::string capability;
    std::string heading;
    std::string content;
    std::string content_hash;
    std::string embedding; // текст pgvector "[0.1,…]"; пусто — без вектора
};

struct Hit {
    long long id = 0;
    std::string repo;
    std::string path;
    std::string kind;
    std::string capability;
    std::string heading;
    std::string content;
};

// Кандидаты двумя способами, каждый уже отсортирован по своей близости и
// отсечён своим порогом (D4).
struct Candidates {
    std::vector<Hit> text;
    std::vector<Hit> vector;
};

// Ключ повторного использования эмбеддинга: (path, heading, content_hash)
// внутри репозитория.
std::string embedding_key(const std::string& path, const std::string& heading, const std::string& content_hash);

// База недоступна или запрос упал: HTTP-слой отвечает 503 (вопрос) или 500
// (заливка — прежний индекс цел).
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Store {
public:
    virtual ~Store() = default;

    // embedding_key → текст вектора для чанков репозитория с вектором.
    virtual std::map<std::string, std::string> embeddings(const std::string& repo) = 0;

    // Удалить чанки репозитория и вставить новые одной транзакцией.
    virtual void replace_repo(const std::string& repo, const std::vector<NewChunk>& chunks) = 0;

    // Полнотекст по question и (если query_vector не пуст) вектор, по limit
    // кандидатов каждым способом, фильтр области — в запросе.
    virtual Candidates search(const Scope& scope, const std::string& question, const std::string& query_vector,
                              std::size_t limit, double max_distance) = 0;
};

} // namespace store
