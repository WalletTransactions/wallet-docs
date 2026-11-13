## MODIFIED Requirements

### Requirement: Область документации по токену
Область SHALL определяться по claim'ам `role` и `platform` проверенного токена: `role` = `admin` — все репозитории и виды (`spec`, `change`, `archive`); `platform` = `android` — репозиторий `android-wallet`, вид `spec`; `platform` = `web` — `wallet-web` и `wallet-web-eqdefault`, вид `spec`; `platform` = `mcp` — `wallet-web`, `wallet-web-eqdefault` и `wallet-mcp`, вид `spec`. Любое другое сочетание SHALL давать пустую область. Токен без `role` (токен MCP и токены обмена) SHALL NOT получать область администратора.

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

#### Scenario: Чужой клиент
- **WHEN** токен с `platform: "other"` и `role: "user"`
- **THEN** область пуста: `GET /api/docs/status` отвечает `403`, а вопрос — `403` без обращения к LLM
