// Гибридный поиск (design D4, спека docs-answering «Гибридный поиск с
// фильтром в SQL», «Порог релевантности»): кандидаты даёт Store, здесь —
// слияние рангов RRF и вспомогательные чистые функции.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "store.hpp"

namespace search {

// По столько кандидатов каждым способом и столько лучших — в промпт.
inline constexpr std::size_t kCandidates = 20;
inline constexpr std::size_t kTop = 8;
// RRF: score = Σ 1 / (k + rank), rank с 1.
inline constexpr double kRrfK = 60.0;
// Вес вклада RRF по виду документа: каноническая спека — истина о текущем
// поведении, change (ещё не влит) и archive (история решений) — вторичны.
// Без веса их кандидатов больше, и они вытесняют spec (замер 6.3).
inline constexpr double kSpecWeight = 1.0;
inline constexpr double kOtherKindWeight = 0.6;
double kind_weight(const std::string& kind);

// Косинусное расстояние (pgvector `<=>`), дальше которого вектор не
// кандидат. Стартовое значение design D4, подбирается в задаче 6.3.
inline constexpr double kMaxVectorDistance = 0.5;

// Слияние по рангу: дубли (по id) складывают вклад, при равенстве счёта
// выше тот, кто раньше в полнотекстовом списке, затем в векторном.
std::vector<store::Hit> fuse(const store::Candidates& c, std::size_t top = kTop, double k = kRrfK);

// Текст pgvector: "[0.1,-0.2,…]".
std::string vector_literal(const std::vector<float>& v);

} // namespace search
