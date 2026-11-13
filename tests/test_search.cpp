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

TEST(fuse_empty) { CHECK(search::fuse(store::Candidates{}).empty()); }

TEST(vector_literal_format) {
    CHECK_EQ(search::vector_literal({0.5f, -1.0f, 0.25f}), std::string("[0.5,-1,0.25]"));
    CHECK_EQ(search::vector_literal({}), std::string("[]"));
}
