# Tasks

## 1. Конфиг и запрос

- [x] 1.1 `src/config.{hpp,cpp}`: поле `llm_reasoning_chat` (строка), читается из `LLM_REASONING_CHAT`: переменная не задана (или пуста) — `none`, `off` — пустая строка (не передавать), остальное как есть. Проверка: `tests/test_config.cpp` — случаи: нет, `low`, `off`, пустая строка
- [x] 1.2 `src/llm.{hpp,cpp}`: `HttpOptions::reasoning_effort`; `chat_body(model, system, user, reasoning_effort)` добавляет `reasoning_effort` только если строка не пуста; `embeddings_body` без изменений. Проверка: `tests/test_llm.cpp` — тело с `none`, с `low`, без поля при пустом; запрос чата к заглушке содержит поле, запрос эмбеддингов — нет
- [x] 1.3 `src/main.cpp`: чат создаётся с `cfg.llm_reasoning_chat`, эмбеддер — с пустым значением; строка старта в журнале показывает `reasoning: <значение>`. Проверка: сборка и `ctest` зелёные

## 2. Документация и версия

- [x] 2.1 README: строка `LLM_REASONING_CHAT` в таблице переменных (умолчание `none`, `off` — не передавать, значения как у API); `VERSION` → `0.1.1`. Проверка: строки в README соответствуют `config.cpp`

## 3. Проверка на test

- [x] 3.1 (проверено 2026-11-13 на test после `0.1.1-rc.1`: оба вопроса — `200` с непустыми `answer` и `sources`, 22,2 с и 8,3 с, в лог старта `reasoning: none`) После rc-тега `wallet-docs 0.1.1-rc.1` и выката на test: вопрос администратора к `/api/docs/ask` отвечает `200` с непустыми `answer` и `sources` быстрее 30 с (два вопроса: архивация кошелька, icon-sync). Проверка: ручная, JWT владельца (токен в чат вставляет владелец, в файлах и GitHub не сохраняется)

## Workflow follow-up

- Архивировать change после 3.1; `docs-service` (5.2) закрывается отдельно.
