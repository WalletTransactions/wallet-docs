# Tasks

## 1. Таблица области

- [ ] 1.1 `scope_for`: платформа `mcp` → `wallet-web`, `wallet-web-eqdefault`, `wallet-mcp`, виды `spec`; порядок проверок прежний (`admin` первой). Проверка: юнит-тесты строки таблицы — токен `mcp` без `role`; `mcp` с `role: "admin"` в токене (синтетический случай) остаётся админом по существующему правилу, а токен MCP без `role` — нет; `platform: "other"` по-прежнему пуст.
- [ ] 1.2 Тест доступа: JWT с `aud` ∋ `wallet-docs`, `scope` ∋ `docs:ask`, `platform: "mcp"`, без `role` проходит `GET /api/docs/status` (`200`), без `docs:ask` — `401`. Проверка: `ctest`.

## 2. Документация

- [ ] 2.1 README (таблица области) и `openapi.yaml` (описание области для `status` и `ask`). Проверка: Swagger UI на `/docs` показывает строку `mcp`; примеры `curl` из README работают.

## 3. Индекс и проверка на стенде

- [ ] 3.1 Убедиться, что `wallet-mcp` заливается в индекс (`docs-index.sh`, список репозиториев). Проверка: в `doc_chunks` есть `repo = wallet-mcp`, `kind = spec`; если нет — добавить репозиторий в список (правка wallet-infra) и залить.
- [ ] 3.2 Выкат на test (rc-тег, по одному сервису). Проверка: токеном с `platform: "mcp"` вопрос про инструменты MCP даёт ответ с источником `wallet-mcp`; вопрос про устройство гейтвея — «В описании системы этого нет.»; токен `platform: "other"` — `403`; остальные платформы отвечают как раньше.

## Заметки к выкату

- Порядок: google-auth `docs-mcp-audience` → wallet-docs `mcp-scope` → wallet-mcp `docs-tool` → `WALLET_DOCS_URL`.
- Архивировать после `docs-service` и после проверки на test.
