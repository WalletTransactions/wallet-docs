# Proposal

## Why

Change `wallet-mcp/docs-tool` даёт MCP-клиентам инструмент `ask_docs`, который задаёт вопрос
wallet-docs JWT с `scope` `docs:ask` и `platform` `mcp` (для клиентов с персональным
токеном `wmcp_…`). Таблица области `scope_for` знает только `android`, `web` и роль
`admin`: для `mcp` она вернёт пустую область, и справка из MCP всегда будет отвечать `403`.

## What Changes

- Платформа `mcp` получает область: репозитории `wallet-web`, `wallet-web-eqdefault` и
  `wallet-mcp`, только вид `spec`.
- Отсутствие `role` у токена не означает права администратора: токены MCP и обмена роли
  не несут, администратор через MCP видит ту же область, что любой клиент MCP.
- Остальные строки таблицы (`admin`, `android`, `web`, любое другое — пусто) не меняются.
- Чат wallet-ai идёт путём обмена RFC 8693, где `platform` наследуется от исходной
  сессии (`android`/`web`): для него правок нет.

## Capabilities

### New Capabilities

### Modified Capabilities
- `docs-answering`: требование «Область документации по токену» — строка `mcp`.

## Impact

- **wallet-docs**: `scope_for` в `src/access.cpp`, `tests/test_access.cpp`, `README.md`
  (таблица области), `openapi.yaml` (описание области).
- **wallet-infra**: проверить, что репозиторий `wallet-mcp` входит в список заливки
  индекса (`docs-index.sh`) — иначе область `mcp` пуста по существу.
- **Зависимости**: google-auth `docs-mcp-audience` выдаёт JWT с нужным адресатом;
  wallet-mcp `docs-tool` его использует. Порядок: google-auth → wallet-docs → wallet-mcp.
- **Порядок архивации**: change правит требование `docs-answering`, которого пока нет в
  основных спеках — оно появится после архивации `docs-service`; `mcp-scope`
  архивируется после него.
