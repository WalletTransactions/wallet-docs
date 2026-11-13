# request-routing Specification

## Purpose

Куда гейтвей отправляет запрос: таблица префиксов пути и её порядок, адреса
апстримов из окружения, фолбэк на статику SPA, поддерживаемые методы, собственные
пути гейтвея и ответ при недоступном апстриме.

## Requirements

### Requirement: Маршруты API
Пути API SHALL уходить в такие апстримы. Все, кроме `/api/mcp/`, проходят
JWT-гейт с указанным адресатом `aud`:

| Префикс | Апстрим | aud |
| --- | --- | --- |
| `/api/reports/` | wallet-report (`WALLET_REPORT_API_URL`) | `wallet-report` |
| `/api/ai/` | wallet-ai (`WALLET_AI_URL`) | `wallet-ai` |
| `/api/mcp/` | wallet-mcp (`WALLET_MCP_URL`) | — (без гейта, см. `edge-jwt-gate`) |
| `/api/` (прочее) | wallet-api (`WALLET_API_URL`) | `wallet-api` |
| `/sync/` | wallet-api (`WALLET_API_URL`) | `wallet-api` |

Путь, query и тело уходят апстриму без изменений. Контракты путей — в
`openapi.yaml` соответствующего апстрима.

#### Scenario: Отчёты раньше общего /api/
- **WHEN** приходит `GET /api/reports/accounts`
- **THEN** запрос уходит в wallet-report, а не в wallet-api

#### Scenario: ИИ раньше общего /api/
- **WHEN** приходит `DELETE /api/ai/jobs/{id}`
- **THEN** запрос уходит в wallet-ai

#### Scenario: MCP раньше общего /api/
- **WHEN** приходит `POST /api/mcp/` с заголовком `Authorization: Bearer wmcp_…`
- **THEN** запрос уходит в wallet-mcp на тот же путь `/api/mcp/` с тем же заголовком, а не в wallet-api

#### Scenario: Синхронизация
- **WHEN** приходит `POST /sync/push`
- **THEN** запрос уходит в wallet-api

### Requirement: Маршруты входа и статики
Пути `/auth/` и `/admin/` SHALL уходить в google-auth (`AUTH_SERVICE_URL`), а
любой другой путь — в wallet-web (`WALLET_WEB_URL`); JWT гейтвей на них не
проверяет.

#### Scenario: Вход и админка
- **WHEN** приходит `GET /auth/me` или `GET /admin/users`
- **THEN** запрос уходит в google-auth без проверки токена гейтвеем

#### Scenario: Путь без префикса
- **WHEN** приходит `GET /join/<token>` или `GET /`
- **THEN** запрос уходит в wallet-web

### Requirement: Порядок и сравнение префиксов
Гейтвей SHALL сравнивать префиксы с путём запроса (`req.path`, без query) в
порядке `/api/reports/`, `/api/ai/`, `/api/mcp/`, `/api/`, `/sync/`, `/auth/`,
`/admin/`; первый совпавший побеждает. Префикс сравнивается вместе с
завершающим `/`.

#### Scenario: Префикс без завершающего слеша
- **WHEN** приходит `GET /api`
- **THEN** запрос уходит в wallet-web как обычный путь SPA, JWT не проверяется

#### Scenario: MCP без завершающего слеша
- **WHEN** приходит `POST /api/mcp` (без `/` в конце) с `Authorization: Bearer wmcp_…`
- **THEN** путь совпадает только с общим `/api/`, запрос проходит JWT-гейт с адресатом `wallet-api` и получает `401` гейтвея; в wallet-mcp он не уходит

### Requirement: Собственные пути гейтвея не проксируются
Гейтвей SHALL сам отвечать на `GET /docs`, `GET /openapi.yaml`,
`GET /openapi/<name>.yaml`, `GET /healthz`, `GET /metrics`, `GET /version`,
`GET /api/versions` и `GET /vendor/swagger-ui/*`, не передавая их апстримам.
Другие методы на этих путях проксируются по общей таблице: на `/api/versions` —
в wallet-api, на остальных — в wallet-web.

#### Scenario: Своя спека вместо спеки веба
- **WHEN** приходит `GET /openapi.yaml`
- **THEN** гейтвей отдаёт свой `openapi.yaml`; спека wallet-web через гейтвей по этому пути недоступна

#### Scenario: Не-GET на служебный путь
- **WHEN** приходит `POST /healthz`
- **THEN** запрос уходит в wallet-web, клиент получает ответ nginx (`405`)

#### Scenario: Сводка версий не уходит в wallet-api
- **WHEN** приходит `GET /api/versions`
- **THEN** отвечает сам гейтвей (см. capability `service-versions`), в wallet-api запрос не передаётся

### Requirement: Поддерживаемые методы
Гейтвей SHALL проксировать методы `GET`, `HEAD`, `POST`, `PUT`, `PATCH`, `DELETE`
и `OPTIONS` на любом пути. Остальные методы гейтвей не проксирует, их отклоняет
cpp-httplib.

#### Scenario: HEAD
- **WHEN** приходит `HEAD /`
- **THEN** ответ — заголовки ответа wallet-web на `GET /` без тела

#### Scenario: Неподдерживаемый метод
- **WHEN** приходит `TRACE /`
- **THEN** ответ — `400`, апстрим не вызывается

### Requirement: Адреса апстримов из окружения
Гейтвей SHALL при старте читать `.env` из рабочей директории (переменные окружения
процесса имеют приоритет) и требовать `AUTH_SERVICE_URL`, `WALLET_API_URL`,
`WALLET_REPORT_API_URL`, `WALLET_AI_URL`, `WALLET_MCP_URL` и `WALLET_WEB_URL`.
Завершающие `/` в адресах отбрасываются. Если любая из них пуста, гейтвей пишет в
журнал `<KEY> must be set — <подсказка> (см. README.md)` и завершается с кодом `1`.

#### Scenario: Нет адреса wallet-ai
- **WHEN** гейтвей запускается без `WALLET_AI_URL`
- **THEN** в журнале `WALLET_AI_URL must be set — адрес wallet-ai, например http://127.0.0.1:8087 (см. README.md)`, процесс завершается с кодом 1

#### Scenario: Нет адреса wallet-mcp
- **WHEN** гейтвей запускается без `WALLET_MCP_URL`
- **THEN** в журнале `WALLET_MCP_URL must be set — адрес wallet-mcp, например http://127.0.0.1:8088 (см. README.md)`, процесс завершается с кодом 1

### Requirement: Недоступный апстрим
Гейтвей SHALL ждать соединения с апстримом до 10 с и ответа до 30 с. Если
соединиться или получить ответ не удалось, клиент получает `502` с телом
`gateway: сервис недоступен` (`text/plain; charset=utf-8`), а в журнал пишется
ошибка `upstream <адрес> недоступен (<причина>) для <МЕТОД> <путь>`.

#### Scenario: Апстрим остановлен
- **WHEN** wallet-report остановлен, а приходит запрос с действительным токеном на `GET /api/reports/accounts`
- **THEN** ответ — `502` с телом `gateway: сервис недоступен`

#### Scenario: Long-poll в пределах таймаута
- **WHEN** приходит `GET /api/ai/jobs/{id}?wait=25`, и wallet-ai отвечает через 25 с
- **THEN** ответ апстрима доходит до клиента, `502` нет
