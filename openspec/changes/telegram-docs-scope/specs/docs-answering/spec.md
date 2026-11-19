## MODIFIED Requirements

### Requirement: Область документации по токену
Область SHALL определяться по claim'ам `role` и `platform` проверенного токена: `role` = `admin` — все репозитории и виды (`spec`, `change`, `archive`); `platform` = `android` — репозиторий `android-wallet`, вид `spec`; `platform` = `web` — `wallet-web` и `wallet-web-eqdefault`, вид `spec`; `platform` = `mcp` — `wallet-web`, `wallet-web-eqdefault` и `wallet-mcp`, вид `spec`; `platform` = `telegram` — `android-wallet`, `google-auth`, `wallet-ai`, `wallet-api`, `wallet-docs`, `wallet-mcp`, `wallet-report`, `wallet-site`, `wallet-telegram`, `wallet-web` и `wallet-web-eqdefault`, вид `spec`. Любое другое сочетание SHALL давать пустую область. Токен без `role` (токен MCP, токены обмена и токен Telegram) SHALL NOT получать область администратора. Репозитории `wallet-gateway`, `wallet-infra` и `wallet-common` и виды `change` и `archive` SHALL быть недоступны любой области без `role` = `admin`.

#### Scenario: Администратор
- **WHEN** запрос приходит с токеном `role: "admin"` и `platform: "web"`
- **THEN** поиск идёт по всем репозиториям и видам

#### Scenario: Пользователь Android
- **WHEN** запрос приходит с токеном `role: "user"`, `platform: "android"`
- **THEN** поиск идёт только по чанкам `android-wallet` вида `spec`

#### Scenario: Клиент MCP
- **WHEN** запрос приходит с токеном без `role` и с `platform: "mcp"`
- **THEN** поиск идёт только по чанкам `wallet-web`, `wallet-web-eqdefault` и `wallet-mcp` вида `spec`

#### Scenario: MCP-токен администратора
- **WHEN** администратор вызывает справку через свой MCP-токен (`platform: "mcp"`, `role` в токене нет)
- **THEN** область — как у клиента MCP, чанки `wallet-gateway` и вид `archive` недоступны

#### Scenario: Пользователь Telegram
- **WHEN** запрос приходит с токеном без `role` и с `platform: "telegram"` (обмен токена чата wallet-ai на `aud` `wallet-docs`)
- **THEN** поиск идёт по чанкам вида `spec` репозиториев `android-wallet`, `google-auth`, `wallet-ai`, `wallet-api`, `wallet-docs`, `wallet-mcp`, `wallet-report`, `wallet-site`, `wallet-telegram`, `wallet-web` и `wallet-web-eqdefault`, `GET /api/docs/status` отвечает `200`

#### Scenario: Закрытое для Telegram
- **WHEN** пользователь Telegram спрашивает про маршрутизацию `wallet-gateway` или про развёртывание `wallet-infra`
- **THEN** чанки этих репозиториев и вид `archive` в поиск не попадают; без подходящих чанков в области ответ — «В описании системы этого нет.»

#### Scenario: Чужой клиент
- **WHEN** токен с `platform: "other"` и `role: "user"`
- **THEN** область пуста: `GET /api/docs/status` отвечает `403`, а вопрос — `403` без обращения к LLM
