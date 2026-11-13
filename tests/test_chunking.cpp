#include <fstream>
#include <sstream>

#include "chunking.hpp"
#include "test_framework.hpp"

namespace {

std::string fixture(const std::string& name) {
    std::ifstream f(std::string(WALLET_DOCS_FIXTURES) + "/" + name, std::ios::binary);
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

} // namespace

TEST(classify_spec) {
    auto i = chunking::classify_path("openspec/specs/sync-run/spec.md");
    REQUIRE(i);
    CHECK_EQ(i->kind, std::string("spec"));
    CHECK_EQ(i->capability, std::string("sync-run"));
}

TEST(classify_archive) {
    auto i = chunking::classify_path("openspec/changes/archive/2026-11-04-add-wallet-mcp-service/design.md");
    REQUIRE(i);
    CHECK_EQ(i->kind, std::string("archive"));
    CHECK_EQ(i->capability, std::string("2026-11-04-add-wallet-mcp-service"));
    auto delta = chunking::classify_path("openspec/changes/archive/2026-11-04-x/specs/mcp/spec.md");
    REQUIRE(delta);
    CHECK_EQ(delta->capability, std::string("2026-11-04-x"));
}

TEST(classify_change) {
    auto i = chunking::classify_path("openspec/changes/docs-service/specs/docs-indexing/spec.md");
    REQUIRE(i);
    CHECK_EQ(i->kind, std::string("change"));
    CHECK_EQ(i->capability, std::string("docs-service"));
}

TEST(classify_skips_foreign) {
    for (const char* p : {"openspec/project.md", "openspec/config.yaml", "src/main.cpp", "../etc/passwd",
                          "openspec/../../etc/passwd.md", "/openspec/specs/a/spec.md", "openspec/specs/a/notes.md",
                          "openspec/specs/spec.md", "openspec/changes/x.md", "openspec/changes/archive/x.md",
                          "openspec\\specs\\a\\spec.md", "openspec//specs/a/spec.md", "README.md",
                          "openspec/changes/x/tasks.txt"}) {
        if (chunking::classify_path(p)) ::testing::report_failure(std::string("должен быть пропущен: ") + p, __FILE__, __LINE__);
    }
}

TEST(spec_requirements_with_scenarios) {
    std::string spec = "# x Specification\n\n## Purpose\n\nЗачем.\n\n## Requirements\n\n";
    for (int r = 1; r <= 3; ++r) {
        spec += "### Requirement: Требование " + std::to_string(r) + "\nТекст " + std::to_string(r) + ".\n\n";
        for (int s = 1; s <= 2; ++s) {
            spec += "#### Scenario: Сценарий " + std::to_string(s) + "\n- **WHEN** a\n- **THEN** b\n\n";
        }
    }
    auto chunks = chunking::chunk_markdown("openspec/specs/x/spec.md", spec);
    REQUIRE(chunks.size() == 4);
    CHECK_EQ(chunks[0].heading, std::string("Purpose"));
    CHECK_EQ(chunks[1].heading, std::string("Требование 1"));
    CHECK(chunks[1].content.rfind("### Requirement: Требование 1", 0) == 0);
    CHECK(chunks[1].content.find("#### Scenario: Сценарий 2") != std::string::npos);
    CHECK(chunks[1].content.find("Требование 2") == std::string::npos);
    CHECK_EQ(chunks[3].heading, std::string("Требование 3"));
}

TEST(delta_spec_split_by_requirement) {
    std::string spec = "# Spec Delta\n\n## Purpose\n\nP.\n\n## ADDED Requirements\n\n"
                       "### Requirement: A\nтекст\n\n#### Scenario: s\n- WHEN x\n\n"
                       "### Requirement: B\nтекст\n\n## REMOVED Requirements\n\n### Requirement: C\nудалено\n";
    auto chunks = chunking::chunk_markdown("openspec/changes/c/specs/x/spec.md", spec);
    REQUIRE(chunks.size() == 4);
    CHECK_EQ(chunks[1].heading, std::string("A"));
    CHECK_EQ(chunks[2].heading, std::string("B"));
    CHECK_EQ(chunks[3].heading, std::string("C"));
}

TEST(design_split_by_h2_and_code_fences) {
    std::string md = "# Design\n\nВступление.\n\n## Context\n\nКонтекст.\n\n```sh\n## не заголовок\n```\n\n"
                     "## Decisions\n\n### D1\nрешение\n";
    auto chunks = chunking::chunk_markdown("openspec/changes/c/design.md", md);
    REQUIRE(chunks.size() == 3);
    CHECK_EQ(chunks[0].heading, std::string("Design"));
    CHECK_EQ(chunks[1].heading, std::string("Context"));
    CHECK(chunks[1].content.find("## не заголовок") != std::string::npos);
    CHECK_EQ(chunks[2].heading, std::string("Decisions"));
    CHECK(chunks[2].content.find("### D1") != std::string::npos);
}

TEST(long_section_split_with_same_heading) {
    std::string para(1000, 'x');
    std::string md = "## Длинная\n\n";
    for (int i = 0; i < 10; ++i) md += para + "\n\n";
    auto chunks = chunking::chunk_markdown("openspec/changes/c/design.md", md);
    REQUIRE(chunks.size() >= 2);
    for (const auto& c : chunks) {
        CHECK_EQ(c.heading, std::string("Длинная"));
        CHECK(c.content.size() <= chunking::kMaxChunkChars);
    }
}

TEST(split_long_respects_utf8) {
    std::string line;
    for (int i = 0; i < 5000; ++i) line += "я"; // 10000 байт одной строкой
    auto parts = chunking::split_long(line, 6000);
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].size() <= 6000);
    CHECK_EQ(parts[0].size() % 2, 0u);
    CHECK_EQ(parts[0] + parts[1], line);
}

TEST(real_gateway_spec) {
    std::string spec = fixture("gateway-request-routing-spec.md");
    REQUIRE(!spec.empty());
    auto chunks = chunking::chunk_markdown("openspec/specs/request-routing/spec.md", spec);
    // Purpose + 7 требований; пустой «## Requirements» не чанк.
    REQUIRE(chunks.size() == 8);
    CHECK_EQ(chunks[0].heading, std::string("Purpose"));
    for (std::size_t i = 1; i < chunks.size(); ++i) {
        CHECK(chunks[i].content.rfind("### Requirement:", 0) == 0);
        CHECK(chunks[i].content.find("#### Scenario:") != std::string::npos);
    }
}

TEST(real_archive_design) {
    std::string md = fixture("archive-add-mcp-route-design.md");
    REQUIRE(!md.empty());
    auto chunks = chunking::chunk_markdown("openspec/changes/archive/2026-11-02-add-mcp-route/design.md", md);
    // «# Design» без текста — не чанк; пять секций ##.
    REQUIRE(chunks.size() == 5);
    CHECK_EQ(chunks[0].heading, std::string("Context"));
    CHECK_EQ(chunks[4].heading, std::string("Migration Plan"));
}
