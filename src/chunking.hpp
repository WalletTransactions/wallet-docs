// Разбор пути файла и нарезка markdown на чанки (спека docs-indexing:
// «Вид и название чанка из пути», «Посторонние файлы пропускаются»,
// «Чанкинг по структуре спеки»). Чистые функции, без базы и сети.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace chunking {

// Чанк длиннее этого (в байтах UTF-8 — для русского текста это ~3000 букв)
// режется по абзацам.
inline constexpr std::size_t kMaxChunkChars = 6000;

struct FileInfo {
    std::string kind;       // spec | change | archive
    std::string capability; // раздел спеки или имя change
};

// nullopt — файл не индексируется (считается в skipped):
// - индексируются только openspec/specs/<раздел>/spec.md и .md внутри
//   openspec/changes/<имя>/ (в том числе archive/<имя>/);
// - .md в корне openspec/ (project.md), файлы вне openspec/, не .md, пути
//   с `..`, абсолютные, с `\` или пустыми сегментами — пропускаются.
std::optional<FileInfo> classify_path(const std::string& path);

struct Chunk {
    std::string heading; // название требования или секции
    std::string content; // markdown чанка, включая строку заголовка
};

// Файл spec.md (любой: и основная спека, и дельта в change): `## Purpose` —
// отдельный чанк, каждый `### Requirement:` со своими `#### Scenario:` —
// отдельный чанк, прочие секции `##` — по одному чанку. Остальные .md — по
// заголовкам `##`; текст до первого `##` — чанк с заголовком `# …` (или
// именем файла). Заголовки внутри блоков ``` не считаются. Чанки без текста
// кроме заголовка отбрасываются; длиннее kMaxChunkChars — режутся по
// абзацам с тем же heading.
std::vector<Chunk> chunk_markdown(const std::string& path, const std::string& content);

// Режет текст по абзацам (пустая строка) на куски не длиннее max_chars;
// абзац длиннее — по строкам, строка длиннее — по границе символа UTF-8.
std::vector<std::string> split_long(const std::string& text, std::size_t max_chars = kMaxChunkChars);

} // namespace chunking
