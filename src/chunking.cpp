#include "chunking.hpp"

#include <algorithm>
#include <sstream>

namespace chunking {

namespace {

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    return parts;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        lines.push_back(cur);
    }
    return lines;
}

// Уровень ATX-заголовка (`## X` → 2) и его текст; 0 — не заголовок.
int heading_level(const std::string& line, std::string* text) {
    std::size_t n = 0;
    while (n < line.size() && line[n] == '#') ++n;
    if (n == 0 || n > 6) return 0;
    if (n < line.size() && line[n] != ' ' && line[n] != '\t') return 0;
    if (text != nullptr) *text = trim(line.substr(n));
    return static_cast<int>(n);
}

bool is_fence(const std::string& line) {
    std::string t = trim(line);
    return starts_with(t, "```") || starts_with(t, "~~~");
}

// Продолжающий байт многобайтного символа UTF-8 (10xxxxxx).
bool is_continuation(unsigned char b) { return (b & 0xC0) == 0x80; }

// Не режем посреди символа: отступаем к началу символа.
std::size_t utf8_cut(const std::string& s, std::size_t pos) {
    if (pos >= s.size()) return s.size();
    while (pos > 0 && is_continuation(static_cast<unsigned char>(s[pos]))) --pos;
    return pos;
}

struct Section {
    std::string heading;
    std::vector<std::string> lines; // включая строку заголовка
};

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out.push_back('\n');
        out += lines[i];
    }
    return trim(out);
}

// Есть ли в секции что-то, кроме строки заголовка.
bool has_body(const Section& s) {
    for (std::size_t i = 1; i < s.lines.size(); ++i) {
        if (!trim(s.lines[i]).empty()) return true;
    }
    return false;
}

std::string file_name(const std::string& path) {
    auto pos = path.rfind('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Режет файл на секции по заголовкам, для которых is_break вернул true.
// Текст до первого такого заголовка — секция с heading = `# …` или именем
// файла.
template <typename Pred>
std::vector<Section> sections(const std::string& path, const std::string& content, Pred is_break) {
    std::vector<Section> out;
    Section cur;
    std::string title;
    bool in_fence = false;
    bool preamble = true;
    for (const std::string& line : split_lines(content)) {
        if (is_fence(line)) in_fence = !in_fence;
        std::string text;
        int level = in_fence ? 0 : heading_level(line, &text);
        if (level == 1 && preamble && title.empty()) title = text;
        if (level > 0 && is_break(level, text)) {
            if (preamble) {
                cur.heading = title.empty() ? file_name(path) : title;
                preamble = false;
            }
            out.push_back(std::move(cur));
            cur = Section{};
            cur.heading = text;
        }
        cur.lines.push_back(line);
    }
    if (preamble) cur.heading = title.empty() ? file_name(path) : title;
    out.push_back(std::move(cur));
    return out;
}

// Преамбула до первой секции: «# Заголовок» и пусто — не чанк.
bool preamble_has_text(const Section& s) {
    for (const std::string& line : s.lines) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (heading_level(line, nullptr) == 1) continue;
        return true;
    }
    return false;
}

} // namespace

std::optional<FileInfo> classify_path(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos) return std::nullopt;
    if (!ends_with(path, ".md")) return std::nullopt;
    std::vector<std::string> parts = split_path(path);
    for (const std::string& p : parts) {
        if (p.empty() || p == "." || p == "..") return std::nullopt;
    }
    if (parts.size() < 3 || parts[0] != "openspec") return std::nullopt;

    if (parts[1] == "specs") {
        // openspec/specs/<раздел>/spec.md — ровно так.
        if (parts.size() == 4 && parts[3] == "spec.md") return FileInfo{"spec", parts[2]};
        return std::nullopt;
    }
    if (parts[1] == "changes") {
        if (parts[2] == "archive") {
            // openspec/changes/archive/<имя>/…/*.md
            if (parts.size() >= 5) return FileInfo{"archive", parts[3]};
            return std::nullopt;
        }
        // openspec/changes/<имя>/…/*.md
        if (parts.size() >= 4) return FileInfo{"change", parts[2]};
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<std::string> split_long(const std::string& text, std::size_t max_chars) {
    if (text.size() <= max_chars) return {text};

    // Единицы — абзацы; абзац длиннее предела — его строки; строка длиннее —
    // куски по границе символа.
    std::vector<std::string> units;
    auto push_line_pieces = [&](const std::string& line) {
        std::size_t pos = 0;
        while (line.size() - pos > max_chars) {
            std::size_t cut = utf8_cut(line, pos + max_chars);
            if (cut <= pos) cut = pos + max_chars; // не UTF-8 — режем как есть
            units.push_back(line.substr(pos, cut - pos));
            pos = cut;
        }
        if (pos < line.size()) units.push_back(line.substr(pos));
    };

    std::vector<std::string> paragraphs;
    {
        std::string cur;
        for (const std::string& line : split_lines(text)) {
            if (trim(line).empty()) {
                if (!trim(cur).empty()) paragraphs.push_back(trim(cur));
                cur.clear();
            } else {
                cur += (cur.empty() ? "" : "\n") + line;
            }
        }
        if (!trim(cur).empty()) paragraphs.push_back(trim(cur));
    }
    for (const std::string& p : paragraphs) {
        if (p.size() <= max_chars) {
            units.push_back(p);
            continue;
        }
        for (const std::string& line : split_lines(p)) push_line_pieces(line);
    }

    // Жадная упаковка единиц в куски через пустую строку (или перевод
    // строки — для кусков одного абзаца, разница несущественна).
    std::vector<std::string> out;
    std::string cur;
    for (const std::string& u : units) {
        if (cur.empty()) {
            cur = u;
        } else if (cur.size() + 2 + u.size() <= max_chars) {
            cur += "\n\n" + u;
        } else {
            out.push_back(cur);
            cur = u;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<Chunk> chunk_markdown(const std::string& path, const std::string& content) {
    const bool is_spec = file_name(path) == "spec.md";
    std::vector<Section> secs;
    if (is_spec) {
        // `##` (Purpose, ADDED Requirements, …) и `### Requirement:` — границы;
        // `#### Scenario:` остаётся внутри требования.
        secs = sections(path, content, [](int level, const std::string& text) {
            return level == 2 || (level == 3 && starts_with(text, "Requirement:"));
        });
    } else {
        secs = sections(path, content, [](int level, const std::string&) { return level == 2; });
    }

    std::vector<Chunk> out;
    for (std::size_t i = 0; i < secs.size(); ++i) {
        Section& s = secs[i];
        bool keep = i == 0 ? preamble_has_text(s) : has_body(s);
        if (!keep) continue;
        std::string heading = s.heading;
        if (is_spec && starts_with(heading, "Requirement:")) heading = trim(heading.substr(12));
        for (std::string& piece : split_long(join(s.lines))) {
            out.push_back(Chunk{heading, std::move(piece)});
        }
    }
    return out;
}

} // namespace chunking
