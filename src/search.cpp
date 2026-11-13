#include "search.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace search {

std::vector<store::Hit> fuse(const store::Candidates& c, std::size_t top, double k) {
    struct Entry {
        const store::Hit* hit = nullptr;
        double score = 0;
        std::size_t first_seen = 0; // порядок первого появления: текст, затем вектор
    };
    std::unordered_map<long long, Entry> by_id;
    std::size_t seen = 0;
    auto add = [&](const std::vector<store::Hit>& list) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const store::Hit& h = list[i];
            auto [it, inserted] = by_id.try_emplace(h.id);
            if (inserted) {
                it->second.hit = &h;
                it->second.first_seen = seen++;
            }
            it->second.score += 1.0 / (k + static_cast<double>(i + 1));
        }
    };
    add(c.text);
    add(c.vector);

    std::vector<Entry> entries;
    entries.reserve(by_id.size());
    for (auto& [id, e] : by_id) entries.push_back(e);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.first_seen < b.first_seen;
    });

    std::vector<store::Hit> out;
    for (std::size_t i = 0; i < entries.size() && i < top; ++i) out.push_back(*entries[i].hit);
    return out;
}

std::string vector_literal(const std::vector<float>& v) {
    std::string out = "[";
    char buf[32];
    for (std::size_t i = 0; i < v.size(); ++i) {
        // 9 значащих цифр — без потерь для float.
        std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v[i]));
        if (i > 0) out.push_back(',');
        out += buf;
    }
    out.push_back(']');
    return out;
}

} // namespace search
