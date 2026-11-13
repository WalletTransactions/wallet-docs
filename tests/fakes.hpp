// Фейки для юнит-тестов: хранилище в памяти, эмбеддер и чат со счётчиками
// вызовов. Поиск в памяти — упрощённый (подстрока), фильтр области — как в SQL.
#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "llm.hpp"
#include "store.hpp"

namespace fakes {

class MemStore : public store::Store {
public:
    std::vector<store::NewChunk> rows;
    bool fail_replace = false;
    bool fail_search = false;
    int searches = 0;
    store::Scope last_scope;
    std::string last_vector;

    std::map<std::string, std::string> embeddings(const std::string& repo) override {
        std::map<std::string, std::string> out;
        for (const auto& r : rows) {
            if (r.repo == repo && !r.embedding.empty()) {
                out[store::embedding_key(r.path, r.heading, r.content_hash)] = r.embedding;
            }
        }
        return out;
    }

    void replace_repo(const std::string& repo, const std::vector<store::NewChunk>& chunks) override {
        // Сбой «до фиксации»: прежние строки не тронуты.
        if (fail_replace) throw store::Error("connection lost");
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const store::NewChunk& r) { return r.repo == repo; }),
                   rows.end());
        rows.insert(rows.end(), chunks.begin(), chunks.end());
    }

    store::Candidates search(const store::Scope& scope, const std::string& question, const std::string& query_vector,
                             std::size_t limit, double) override {
        ++searches;
        last_scope = scope;
        last_vector = query_vector;
        if (fail_search) throw store::Error("database down");
        store::Candidates c;
        long long id = 0;
        for (const auto& r : rows) {
            ++id;
            bool in_scope = (scope.all || std::find(scope.repos.begin(), scope.repos.end(), r.repo) != scope.repos.end()) &&
                            std::find(scope.kinds.begin(), scope.kinds.end(), r.kind) != scope.kinds.end();
            if (!in_scope || r.content.find(question) == std::string::npos) continue;
            if (c.text.size() >= limit) break;
            c.text.push_back({id, r.repo, r.path, r.kind, r.capability, r.heading, r.content});
        }
        return c;
    }

    std::size_t count(const std::string& repo) const {
        return static_cast<std::size_t>(
            std::count_if(rows.begin(), rows.end(), [&](const store::NewChunk& r) { return r.repo == repo; }));
    }
};

class FakeEmbedder : public llm::Embedder {
public:
    bool fail = false;
    int calls = 0;
    std::size_t texts = 0;
    std::vector<std::string> inputs;

    llm::EmbedResult embed(const std::vector<std::string>& in) override {
        ++calls;
        texts += in.size();
        inputs.insert(inputs.end(), in.begin(), in.end());
        llm::EmbedResult r;
        if (fail) {
            r.error = "Connection";
            return r;
        }
        r.ok = true;
        for (std::size_t i = 0; i < in.size(); ++i) r.vectors.emplace_back(llm::kEmbeddingDim, 0.5f);
        return r;
    }
};

class FakeChat : public llm::Chat {
public:
    bool fail = false;
    int calls = 0;
    std::string reply = "Ответ [1].";
    std::string last_user;

    llm::ChatResult complete(const std::string&, const std::string& user) override {
        ++calls;
        last_user = user;
        llm::ChatResult r;
        if (fail) {
            r.error = "Connection";
            return r;
        }
        r.ok = true;
        r.content = reply;
        return r;
    }
};

} // namespace fakes
