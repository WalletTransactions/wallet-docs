#include "indexer.hpp"

#include <algorithm>
#include <map>

#include "chunking.hpp"
#include "search.hpp"
#include "util/crypto.hpp"

namespace store {

std::string embedding_key(const std::string& path, const std::string& heading, const std::string& content_hash) {
    // \x1f (unit separator) не встречается в путях и заголовках markdown.
    return path + '\x1f' + heading + '\x1f' + content_hash;
}

} // namespace store

namespace indexer {

namespace {

std::string to_hex(const std::vector<unsigned char>& bytes) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

std::size_t utf8_floor(const std::string& s, std::size_t pos) {
    if (pos >= s.size()) return s.size();
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) --pos;
    return pos;
}

} // namespace

bool valid_repo_name(const std::string& repo) {
    if (repo.empty() || repo.size() > 64) return false;
    for (std::size_t i = 0; i < repo.size(); ++i) {
        char c = repo[i];
        bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum && !(i > 0 && c == '-')) return false;
    }
    return true;
}

std::string content_hash(const std::string& content) { return to_hex(crypto::sha256(content)); }

std::string embed_text(const std::string& heading, const std::string& content) {
    std::string body = content.size() > kMaxEmbedChars ? content.substr(0, utf8_floor(content, kMaxEmbedChars))
                                                       : content;
    return std::string(llm::kDocumentPrefix) + heading + "\n\n" + body;
}

Stats index_repo(store::Store& db, llm::Embedder* embedder, const std::string& repo, const std::vector<File>& files) {
    Stats stats;
    std::vector<store::NewChunk> chunks;
    for (const File& f : files) {
        auto info = chunking::classify_path(f.path);
        if (!info) {
            ++stats.skipped;
            continue;
        }
        for (chunking::Chunk& c : chunking::chunk_markdown(f.path, f.content)) {
            store::NewChunk n;
            n.repo = repo;
            n.path = f.path;
            n.kind = info->kind;
            n.capability = info->capability;
            n.heading = std::move(c.heading);
            n.content = std::move(c.content);
            n.content_hash = content_hash(n.content);
            chunks.push_back(std::move(n));
        }
    }
    stats.chunks = chunks.size();

    // Прежние векторы — до транзакции; эмбеддинги тоже (D6): транзакция
    // замены короткая и не ждёт LLM.
    std::map<std::string, std::string> previous = db.embeddings(repo);
    std::vector<std::size_t> todo;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        auto it = previous.find(store::embedding_key(chunks[i].path, chunks[i].heading, chunks[i].content_hash));
        if (it != previous.end()) {
            chunks[i].embedding = it->second;
            ++stats.reused;
        } else {
            todo.push_back(i);
        }
    }

    if (embedder == nullptr && !todo.empty()) stats.embed_error = "embeddings_disabled";
    for (std::size_t start = 0; embedder != nullptr && start < todo.size(); start += kEmbedBatch) {
        std::size_t end = std::min(todo.size(), start + kEmbedBatch);
        std::vector<std::string> texts;
        for (std::size_t j = start; j < end; ++j) {
            const store::NewChunk& c = chunks[todo[j]];
            texts.push_back(embed_text(c.heading, c.content));
        }
        llm::EmbedResult r = embedder->embed(texts);
        if (!r.ok) {
            // Недоступные эмбеддинги не блокируют заливку: остаток — без
            // векторов, ищется полнотекстом.
            stats.embed_error = r.error.empty() ? "embeddings_failed" : r.error;
            break;
        }
        for (std::size_t j = start; j < end; ++j) {
            chunks[todo[j]].embedding = search::vector_literal(r.vectors[j - start]);
            ++stats.embedded;
        }
    }

    db.replace_repo(repo, chunks);
    return stats;
}

} // namespace indexer
