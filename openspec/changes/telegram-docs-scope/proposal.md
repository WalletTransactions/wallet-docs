# Proposal

## Why

Telegram-бот отвечает на вопросы пользователя через чат wallet-ai: модель читает инструменты wallet-mcp, среди них `ask_docs` — справка по описанию системы. Но JWT Telegram-пользователя несёт `platform: "telegram"`, а в таблице области wallet-docs такой платформы нет: область пуста, и справка отвечает `403 {"error":"forbidden"}`. Инструмент `ask_docs` в `tools/list` у чата есть, но на вопрос «как работает баланс?» или «что делает переоценка?» он вернёт «Справка недоступна для этого токена.». Пользователь Telegram получает ответ про траты и не получает ответ про устройство системы.

Токен обмена наследует `platform` от исходного JWT (спека `token-exchange` google-auth), поэтому отказывает именно таблица области wallet-docs; остальные сервисы цепочки (wallet-ai → wallet-mcp → google-auth → wallet-docs) пропускают `telegram` без правок.

## What Changes

- В таблицу области добавляется `platform: telegram`: все продуктовые репозитории индекса, вид `spec` — `android-wallet`, `google-auth`, `wallet-ai`, `wallet-api`, `wallet-docs`, `wallet-mcp`, `wallet-report`, `wallet-site`, `wallet-telegram`, `wallet-web`, `wallet-web-eqdefault`. Решение владельца 2026-11-19: Telegram отвечает на любые вопросы про любой продукт.
- Не входят в область Telegram, как и в область MCP: `wallet-gateway`, `wallet-infra`, `wallet-common` (внутренняя маршрутизация, развёртывание, общий код) и виды `change` и `archive` (только `role: admin`).
- Токен без `role` и с `platform: "telegram"` по-прежнему не получает область администратора.
- Путь доступа не меняется: `ask_docs` в чате wallet-ai, без новых вызовов из wallet-telegram.

## Capabilities

### New Capabilities

Нет.

### Modified Capabilities

- `docs-answering`: требование «Область документации по токену» — добавляется `platform: telegram`.

## Impact

- `wallet-docs`: константа таблицы области (`src/access.cpp`), тест `scope_table`, таблица области в `README.md`, при необходимости описание в `openapi.yaml`.
- `wallet-mcp`, `wallet-ai`, `wallet-telegram`, `google-auth`, `wallet-gateway`: код не меняется. После выкатки wallet-docs проверка живым вопросом в Telegram-чате; если модель не вызывает `ask_docs` на вопрос «как работает…», это отдельная правка описания инструмента в wallet-mcp (вне этого change).
- Сервис необязательный (compose-профиль `docs`): без него `ask_docs` скрыт, и поведение Telegram не меняется.
- Безопасность: область растёт для одной платформы; закрытые репозитории и виды остаются закрытыми, область определяет только сервер по claim'ам проверенного токена.
