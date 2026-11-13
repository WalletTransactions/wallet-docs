#include "search.hpp"
#include "test_framework.hpp"

namespace {
store::Hit hit(long long id) {
    store::Hit h;
    h.id = id;
    h.heading = "h" + std::to_string(id);
    return h;
}
} // namespace

TEST(fuse_rewards_both_lists) {
    store::Candidates c;
    c.text = {hit(1), hit(2), hit(3)};
    c.vector = {hit(3), hit(4)};
    auto top = search::fuse(c, 8);
    REQUIRE(top.size() == 4);
    // 3: 1/63 + 1/61 — выше, чем 1: 1/61.
    CHECK_EQ(top[0].id, 3);
    CHECK_EQ(top[1].id, 1);
}

TEST(fuse_tie_prefers_text_then_vector) {
    store::Candidates c;
    c.text = {hit(1)};
    c.vector = {hit(2)};
    auto top = search::fuse(c, 8);
    REQUIRE(top.size() == 2);
    CHECK_EQ(top[0].id, 1);
    CHECK_EQ(top[1].id, 2);
}

TEST(fuse_limits_to_top) {
    store::Candidates c;
    for (int i = 0; i < 20; ++i) c.text.push_back(hit(i));
    for (int i = 100; i < 120; ++i) c.vector.push_back(hit(i));
    CHECK_EQ(search::fuse(c).size(), search::kTop);
}

namespace {
store::Hit hit_kind(long long id, const char* kind) {
    store::Hit h = hit(id);
    h.kind = kind;
    return h;
}
} // namespace

TEST(fuse_prefers_spec_over_archive_at_close_rank) {
    store::Candidates c;
    // archive на 1-м месте, spec на 2-м: вес переводит spec вперёд.
    c.text = {hit_kind(1, "archive"), hit_kind(2, "spec")};
    auto top = search::fuse(c, 8);
    REQUIRE(top.size() == 2);
    CHECK_EQ(top[0].id, 2);
    CHECK_EQ(top[1].id, 1);
}

TEST(fuse_archive_far_ahead_still_wins) {
    store::Candidates c;
    // archive на 1-м в обоих списках, spec только на 15-м в одном: вес — не запрет.
    c.text = {hit_kind(1, "archive")};
    c.vector = {hit_kind(1, "archive")};
    for (int i = 0; i < 13; ++i) c.text.push_back(hit_kind(100 + i, "change"));
    c.text.push_back(hit_kind(2, "spec"));
    auto top = search::fuse(c, 8);
    REQUIRE(!top.empty());
    CHECK_EQ(top[0].id, 1);
}

TEST(kind_weight_values) {
    CHECK(search::kind_weight("spec") > search::kind_weight("archive"));
    CHECK_EQ(search::kind_weight("change"), search::kind_weight("archive"));
}

TEST(fuse_empty) { CHECK(search::fuse(store::Candidates{}).empty()); }

TEST(vector_literal_format) {
    CHECK_EQ(search::vector_literal({0.5f, -1.0f, 0.25f}), std::string("[0.5,-1,0.25]"));
    CHECK_EQ(search::vector_literal({}), std::string("[]"));
}
