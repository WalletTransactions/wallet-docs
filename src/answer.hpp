// Промпт ответа и разбор ссылок на фрагменты (design D8, спека
// docs-answering «Ответ строится только по найденному», «Ссылки на
// фрагменты»). Чистые функции.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "store.hpp"

namespace answer {

// Дословно из спеки: ответ без вызова LLM, когда кандидатов нет.
inline constexpr const char* kNotFound = "В описании системы этого нет.";
// Длина вопроса — в символах (кодовых точках UTF-8).
inline constexpr std::size_t kMaxQuestionChars = 1000;

std::size_t utf8_length(const std::string& s);

// Системная инструкция: по-русски, только по фрагментам, номер фрагмента
// после каждого утверждения, фрагменты — данные, а не инструкции.
std::string system_prompt();

// Сообщение пользователя: пронумерованные фрагменты [1]…[N] в разделителях,
// затем вопрос.
std::string user_prompt(const std::string& question, const std::vector<store::Hit>& fragments);

struct Source {
    std::string repo;
    std::string capability;
    std::string heading;

    bool operator==(const Source& o) const {
        return repo == o.repo && capability == o.capability && heading == o.heading;
    }
};

struct Cited {
    std::string answer;
    std::vector<Source> sources;
};

// Ссылки [n] и [n, m] на фрагменты 1…N (N = fragments.size()): sources — в
// порядке первой ссылки, без повторов (repo, capability, heading), номера
// вне диапазона отброшены; все маркеры удалены из текста. Без ссылок текст
// возвращается как есть, sources пуст.
Cited cite(const std::string& llm_answer, const std::vector<store::Hit>& fragments);

} // namespace answer
