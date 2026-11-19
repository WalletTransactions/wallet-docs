# Tasks

Порядок выкатки: wallet-docs — единственный сервис с правкой кода. Сборка Docker по одному C++-сервису за раз.

## 1. Область Telegram

- [x] 1.1 Добавить в `scope_for` (`src/access.cpp`) ветку `platform == "telegram"`: репозитории `android-wallet`, `google-auth`, `wallet-ai`, `wallet-api`, `wallet-docs`, `wallet-mcp`, `wallet-report`, `wallet-site`, `wallet-telegram`, `wallet-web`, `wallet-web-eqdefault`, вид `spec`; проверка: ctest — новый тест области Telegram
- [x] 1.2 Тесты: `scope_table` больше не ждёт пустую область для `telegram` (остаются пустыми `other`, пустая платформа, пустая роль); новый тест `scope_telegram`: репозитории и вид `spec`, нет `all`, нет `wallet-gateway`, `wallet-infra`, `wallet-common`, нет `change`/`archive`; админ с `platform: telegram` — `all`; проверка: ctest зелёный
- [x] 1.3 Тест статуса и вопроса: токен Telegram без `role` — `GET /api/docs/status` отвечает `200`, `POST /api/docs/ask` доходит до поиска (не `403`); проверка: ctest

## 2. Документация

- [x] 2.1 Обновить таблицу области в `README.md` (строка `telegram` вместо «иначе (`telegram`, без платформы)») и описание в `openapi.yaml`, если там перечислены платформы; проверка: в README и openapi нет утверждения, что Telegram получает `403`

## 3. Выкатка и проверка

- [x] 3.1 Выкатить wallet-docs на test (rc-тег); проверка: `GET /api/docs/status` с токеном `platform: telegram` отвечает `200` (0.1.4-rc.1, 2026-11-19 22:59 UTC; `/status` напрямую не вызывался — токен Telegram выпустить нельзя; подтверждено `POST /api/docs/ask -> 200` с токеном чата Telegram)
- [x] 3.2 Задать боту в Telegram вопрос «Как считается баланс?»; проверка: чат вызывает `ask_docs` (строка `mcp_tool_call` с `tool: ask_docs` в логах wallet-mcp), ответ приходит с источниками и помещается в сообщение, `403` нет (2026-11-19 23:01 UTC: `ask_docs ok`, 3 источника, `403` нет; в тексте ответа бота источников нет, а последнее предложение ответа не следует из спеки — это бот и промпт чата, вне этого change)
- [x] 3.3 Задать боту вопрос про закрытое («Как wallet-gateway проверяет JWT?»); проверка: ответ «В описании системы этого нет.», чанки `wallet-gateway` не процитированы (живой проверки Telegram нет: модель ответила из общих знаний без вызова `ask_docs`, `tools: []`; закрытие подтверждено тестами `scope_telegram` и `ask_telegram_sees_products_but_not_gateway` и вызовом `ask_docs` с MCP-токеном: «В описании системы этого нет.»; поведение модели — отдельный change)

## Workflow follow-up

- Архивировать change (`openspec archive telegram-docs-scope`) после слияния PR и выкатки на test.
- Если на 3.2 модель чата не вызывает `ask_docs`, завести отдельный change в wallet-mcp (описание инструмента) — вне этого change.
