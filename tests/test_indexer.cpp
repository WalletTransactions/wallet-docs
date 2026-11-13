#include "fakes.hpp"
#include "indexer.hpp"
#include "test_framework.hpp"

namespace {

std::string spec(const std::string& name, int requirements) {
    std::string s = "# " + name + "\n\n## Purpose\n\nНазначение " + name + ".\n\n## Requirements\n\n";
    for (int i = 1; i <= requirements; ++i) {
        s += "### Requirement: " + name + " " + std::to_string(i) + "\nТекст.\n\n#### Scenario: s\n- WHEN a\n";
    }
    return s;
}

std::vector<indexer::File> repo_files(int specs) {
    std::vector<indexer::File> files;
    for (int i = 0; i < specs; ++i) {
        std::string cap = "cap" + std::to_string(i);
        files.push_back({"openspec/specs/" + cap + "/spec.md", spec(cap, 2)});
    }
    return files;
}

} // namespace

TEST(repo_name_validation) {
    CHECK(indexer::valid_repo_name("android-wallet"));
    CHECK(indexer::valid_repo_name("a"));
    CHECK(!indexer::valid_repo_name("Android Wallet"));
    CHECK(!indexer::valid_repo_name("-wallet"));
    CHECK(!indexer::valid_repo_name(""));
    CHECK(!indexer::valid_repo_name(std::string(65, 'a')));
    CHECK(indexer::valid_repo_name(std::string(64, 'a')));
    CHECK(!indexer::valid_repo_name("wallet_web"));
}

TEST(first_upload_embeds_everything) {
    fakes::MemStore db;
    fakes::FakeEmbedder emb;
    auto files = repo_files(3);
    files.push_back({"openspec/project.md", "# p\n\ntext"});
    files.push_back({"src/main.cpp", "int main(){}"});
    auto s = indexer::index_repo(db, &emb, "android-wallet", files);
    CHECK_EQ(s.chunks, 9u); // 3 × (Purpose + 2 требования)
    CHECK_EQ(s.embedded, 9u);
    CHECK_EQ(s.reused, 0u);
    CHECK_EQ(s.skipped, 2u);
    CHECK_EQ(db.count("android-wallet"), 9u);
    REQUIRE(!emb.inputs.empty());
    CHECK(emb.inputs[0].rfind("search_document: ", 0) == 0);
}

TEST(repeat_upload_reuses_without_requests) {
    fakes::MemStore db;
    fakes::FakeEmbedder emb;
    indexer::index_repo(db, &emb, "android-wallet", repo_files(3));
    int calls = emb.calls;
    auto s = indexer::index_repo(db, &emb, "android-wallet", repo_files(3));
    CHECK_EQ(s.reused, 9u);
    CHECK_EQ(s.embedded, 0u);
    CHECK_EQ(emb.calls, calls);
    CHECK_EQ(db.count("android-wallet"), 9u); // без дублей
}

TEST(changed_file_embeds_only_its_chunks) {
    fakes::MemStore db;
    fakes::FakeEmbedder emb;
    indexer::index_repo(db, &emb, "r", repo_files(100));
    auto files = repo_files(100);
    files[42].content += "\nДобавлено в Purpose? нет — в последнее требование.\n";
    std::size_t before = emb.texts;
    auto s = indexer::index_repo(db, &emb, "r", files);
    CHECK_EQ(s.embedded, 1u);
    CHECK_EQ(emb.texts - before, 1u);
    CHECK_EQ(s.reused, s.chunks - 1);
}

TEST(removed_file_disappears_other_repos_untouched) {
    fakes::MemStore db;
    fakes::FakeEmbedder emb;
    indexer::index_repo(db, &emb, "a", repo_files(2));
    indexer::index_repo(db, &emb, "b", repo_files(1));
    auto files = repo_files(2);
    files.pop_back();
    indexer::index_repo(db, &emb, "a", files);
    CHECK_EQ(db.count("a"), 3u);
    CHECK_EQ(db.count("b"), 3u);
}

TEST(embeddings_unavailable_still_indexes) {
    fakes::MemStore db;
    fakes::FakeEmbedder emb;
    emb.fail = true;
    auto s = indexer::index_repo(db, &emb, "a", repo_files(20)); // 60 чанков, 4 пачки
    CHECK_EQ(s.embedded, 0u);
    CHECK_EQ(s.chunks, 60u);
    CHECK_EQ(emb.calls, 1); // после первой неудачи пачки не шлются
    CHECK(!s.embed_error.empty());
    CHECK_EQ(db.count("a"), 60u);
    for (const auto& r : db.rows) CHECK(r.embedding.empty());
}

TEST(no_embedder_keeps_previous_vectors) {
    fakes::MemStore db;
    fakes::FakeEmbedder emb;
    indexer::index_repo(db, &emb, "a", repo_files(1));
    auto s = indexer::index_repo(db, nullptr, "a", repo_files(1));
    CHECK_EQ(s.reused, 3u);
    CHECK_EQ(s.embedded, 0u);
}

TEST(failed_replace_keeps_old_index) {
    fakes::MemStore db;
    fakes::FakeEmbedder emb;
    indexer::index_repo(db, &emb, "a", repo_files(2));
    db.fail_replace = true;
    CHECK_THROWS(indexer::index_repo(db, &emb, "a", repo_files(1)));
    CHECK_EQ(db.count("a"), 6u);
}

TEST(embed_text_truncates_on_char_boundary) {
    std::string content;
    for (int i = 0; i < 3000; ++i) content += "ж";
    std::string t = indexer::embed_text("H", content);
    CHECK(t.rfind("search_document: H\n\n", 0) == 0);
    CHECK(t.size() <= indexer::kMaxEmbedChars + 40);
    CHECK_EQ((t.size() - std::string("search_document: H\n\n").size()) % 2, 0u);
}
