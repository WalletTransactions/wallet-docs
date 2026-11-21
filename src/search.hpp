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
inline constexpr std::size_t kTop = 10;
// RRF: score = Σ 1 / (k + rank), rank с 1.
inline constexpr double kRrfK = 60.0;
// Вес вклада RRF по виду документа: каноническая спека — истина о текущем
// поведении, change (ещё не влит) и archive (история решений) — вторичны.
// Без веса их кандидатов больше, и они вытесняют spec (замер 6.3). ops (README
// по развёртыванию, только админу) весит как spec: иначе при 20 кандидатах-спеках
// он не попадает в топ-10 (change docs-ops-kind, поправка веса).
inline constexpr double kSpecWeight = 1.0;
inline constexpr double kOtherKindWeight = 0.6;
double kind_weight(const std::string& kind);

// Косинусное расстояние (pgvector `<=>`), дальше которого вектор не
// кандидат. Стартовое значение design D4, подбирается в задаче 6.3.
inline constexpr double kMaxVectorDistance = 0.5;

// Сигнал «не по теме» до LLM (design docs-answer-quality, D2; замер 3.1, README
// «Качество ответов»): ни одного полнотекстового кандидата, то есть ни одно слово
// вопроса не встречается в видимых спеках. Расстояние первого чанка и разрыв
// 1-го/2-го кандидата вопросы по теме и не по теме не разделяют (интервалы
// пересекаются), а ts_rank = 0 — у 5 из 8 нейтральных вопросов и ни у одного из
// 19 вопросов набора по теме (наименьший ts_rank у них 0,0515).
bool is_off_topic(const store::Candidates& c);

// Слияние по рангу: дубли (по id) складывают вклад, при равенстве счёта
// выше тот, кто раньше в полнотекстовом списке, затем в векторном.
std::vector<store::Hit> fuse(const store::Candidates& c, std::size_t top = kTop, double k = kRrfK);

// Текст pgvector: "[0.1,-0.2,…]".
std::string vector_literal(const std::vector<float>& v);

} // namespace search
