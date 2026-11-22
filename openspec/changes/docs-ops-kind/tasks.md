# Tasks

Порядок выкатки: wallet-docs с миграцией и видом, затем (отдельным change в wallet-infra) отбор файлов в `docs-index.sh`. Сборка Docker по одному C++-сервису за раз. Версия сервиса растёт с `0.1.5` до `0.1.6`.

## 1. Разбор пути

- [x] 1.1 В `classify_path` (`src/chunking.cpp`, `src/chunking.hpp`) добавить вид `ops`: файлы с именем ровно `README.md` любой глубины; `capability` — каталог файла (`README`, `infra/env`); правила `openspec/` старше, `README.md` внутри `openspec/` по-прежнему `nullopt`; проверка: ctest — тесты `classify_ops_readme`, `classify_ops_nested_readme`, `classify_ops_other_names_skipped` (`docs/TODO_x.md`, `readme.md`, `NOTES.md`), `classify_openspec_readme_skipped`
- [x] 1.2 Отсечь служебные каталоги (`third_party/`, `node_modules/`, `.worktrees/`, `build/`, `vendor/`); проверка: ctest — `classify_ops_vendor_skipped`, а прежние тесты `spec`/`change`/`archive` и `..` без изменений зелёные
- [x] 1.3 Тест индексатора: заливка с `README.md`, `infra/env/README.md`, `docs/TODO_x.md`, `third_party/x/README.md`, `src/main.cpp` даёт чанки `ops` для первых двух и `skipped` = 3 (`tests/test_indexer.cpp`); проверка: ctest зелёный
- [x] 1.4 Чанкинг `ops`: README с вводным абзацем и тремя секциями `##` даёт четыре чанка, блок кода с `##` внутри не режет (`tests/test_chunking.cpp`); проверка: ctest зелёный

## 2. Миграция БД

- [x] 2.1 `migrations/000002_doc_chunks_kind_ops.up.sql`: снять `CHECK` на `kind` и создать заново с `spec`, `change`, `archive`, `ops`; `000002…down.sql`: удалить чанки `ops` и вернуть прежний `CHECK`; `000001` не трогать; проверка: на throwaway `postgres:16` накатить up, вставить чанк `ops` (успех), накатить down (чанков `ops` нет, вставка `ops` отвергается), накатить up повторно
- [x] 2.2 Проверить права роли сервиса после миграции (только DML, как после `000001`); проверка: `psql` под ролью сервиса вставляет и удаляет чанк `ops`, DDL отвергается (2026-11-22, test, `SET ROLE wallet_docs` в транзакции с откатом: `CHECK` содержит `ops`; `INSERT`/`DELETE` чанка `ops` прошли; `ALTER`/`DROP TABLE` — «must be owner», `TRUNCATE` и `CREATE TABLE` в `public` — «permission denied»; остатков `perm-check` нет)

## 3. Область, метка и вес

- [x] 3.1 В `scope_for` (`src/access.cpp`) дописать `ops` только в ветку `role == "admin"`; ветки `android`, `web`, `mcp`, `telegram` не менять; проверка: ctest — `scope_admin_has_ops`, `scope_user_platforms_have_no_ops` (android, web, mcp, telegram, other), `scope_telegram_without_role_has_no_ops`
- [x] 3.2 `kind_label` (`src/answer.cpp`): «справка по развёртыванию (README, может отставать от спек)» для `ops`; `kind_weight` (`src/search.cpp`) для `ops` = `kOtherKindWeight`; проверка: ctest — тест состава промпта с чанком `ops` и тест, что при равной близости `spec` ранжируется выше `ops`
- [x] 3.3 HTTP-тесты (`tests/test_http.cpp`): токен админа ищет по `ops`, токен `user`/`android` — нет, токен без `role` с `platform: "mcp"` — нет; проверка: ctest зелёный

## 4. Документация

- [x] 4.1 `openapi.yaml`: описание `PUT /docs/index/{repo}` (какие файлы индексируются, вид `ops`, пропуск служебных каталогов) и поля `kind` в `sources` (значение `ops`); проверка: `GET /openapi.yaml` на локальном запуске отдаёт новое описание, Swagger UI открывается
- [x] 4.2 `README.md`: раздел «Заливка индекса» (правила `ops`, `capability`), таблица области (`ops` — только `admin`), схема таблицы (`kind`); `VERSION` → `0.1.6`; проверка: в README нет утверждения «индексируются только `openspec/…`», команды из новых абзацев выполняются как написано

## 5. Данные и качество

- [x] 5.1 Перед первой заливкой пройти по всем `README.md` репозиториев поиском значений токенов, паролей и приватных ключей (`grep -rIn -E 'token|password|secret|PRIVATE KEY'` с просмотром совпадений); проверка: результат записан в tasks.md (что найдено, что вычищено или признано именем переменной без значения) (2026-11-21: 54 README в WalletTransactions без третьих каталогов, шаблон токенов/паролей/ключей/JWT/PAT/Telegram-токенов/`wmcp_` совпадений не дал; значения секретов в README не хранятся)
- [x] 5.2 Расширить контрольный набор (`tests/quality/questions.json`, `scripts/quality.sh`) 4 вопросами админа про развёртывание (mTLS-переключатели, порядок выкатки, переменные `DOCS_*`, rc-теги) с ожидаемым `ops` и 2 вопросами пользователя Android на те же темы с ожиданием «ответа нет»; проверка: прогон набора на test, итог не хуже прежнего «17 из 20» по старым вопросам, новые админские находят `ops`, пользовательские отвечают «В описании системы этого нет.»; при провале админских — повысить вес `ops` и записать число в design D7 (2026-11-22, `0.1.7-rc.1`, модель `wallet-parse` = qwen3-8b, контекст 16384 (8192 не хватало: промпт до 9497 токенов → `400` LLM → `503`): полный набор 20 из 26, ложных отказов 0, p95 59,4 с. Новые 21–26: 6 из 6 (админ находит `ops` — `infra/env`, `wallet-infra/README`; Android-пользователь получает «В описании системы этого нет.»). Старые 20: 14 против прежних 17 — не лучше, критерий НЕ выполнен: промахи #15, #17, #18 (holdout), #19 — в `sources` у админа вместо спек стоят README (`google-auth/README`, `wallet-api/README`, `wallet-telegram/README`), то есть вес `ops` = 1.0 вытесняет спеки; #1 и #11 — старые промахи. Оговорка: прежние 17 снимались на `gemma-4-e4b`, сейчас Qwen3-8B, один прогон шумит на ±1 — нужен прогон без `ops` на той же модели для честного сравнения; затем вес `ops` 0.8 (VERSION 0.1.8, PR #27, `v0.1.8-rc.1` на test, Qwen3-8B, контекст 16384): 21 из 26, ложных отказов 2, p95 21,2 с; старые 20 — 17 (как раньше; #15, #17, #18 вернулись, промахи только прежние #1, #11, #19), новые 21–26 — 4 из 6: #25, #26 (Android) и #23, #24 (админ) верно, #21 — в `sources` README других сервисов (`wallet-telegram`, `wallet-mcp`, `wallet-ai`) вместо `infra/env`, #22 — ложный отказ; при весе 1.0 новые были 6 из 6, но старые 14) — владелец 2026-11-22 принял вес 0.8 с оговоркой: #21 и #22 остаются слабым местом (README соседних сервисов про mTLS отбирают место у `infra/env`; #22 — ложный отказ), критерий «новые админские находят `ops`» выполнен на 2 из 4

## 6. Выкатка и проверка

- [x] 6.1 Выкатить wallet-docs на test (rc-тег, один C++-сервис за раз, миграция вместе с кодом); проверка: `GET /healthz` и `GET /version` показывают `0.1.6-rc.N`, сервис стартует, чанки прежних видов целы (2026-11-21: `0.1.6-rc.1` ok 19:20 UTC, затем `0.1.7-rc.1` ok 20:59 UTC — вес ops как у spec)
- [x] 6.2 Залить на test `README.md` и `infra/env/README.md` репозитория `wallet-infra` вместе с его `openspec/` вручную через `PUT /docs/index/{repo}` с `X-Index-Token`; проверка: ответ `200`, `chunks` > 0, `skipped` равен числу посторонних файлов, чанки `ops` есть в таблице (2026-11-21: `wallet-infra` с README — `chunks 333`, `ops` 34; затем `docs-index.sh test all`, 13 репозиториев; заливка по mTLS)
- [x] 6.3 Спросить админским токеном «Как включить mTLS для служебного порта google-auth?»; проверка: ответ `200`, в `sources` есть `infra/env` с видом `ops` (2026-11-21, 0.1.7-rc.1, админ: ответ по mTLS, `sources` — README `wallet-mcp`, `wallet-telegram`, `wallet-infra` (`infra/env`, обе секции); на 0.1.6-rc.1 было «В описании системы этого нет.» — причина вес 0.6)
- [ ] 6.4 Спросить тот же вопрос токеном `platform: "android"`, затем токеном `platform: "telegram"` и MCP-токеном админа; проверка: во всех трёх ответ «В описании системы этого нет.», `sources` без `ops` (2026-11-22, на `0.1.7-rc.1`: пользователь без `role` с `platform: android` — отказ на вопросы про mTLS и `infra/env`, `sources` пусты (#25, #26); MCP-токен админа (`wmcp_`, выпущен и отозван) через `ask_docs` на вопрос про mTLS — «В описании системы этого нет.», `sources` пусты; `platform: telegram` вживую не проверялся — бот ходит в справку только через чат wallet-ai (модель сама решает вызвать `ask_docs`), покрыто тестами `scope_telegram_without_role_has_no_ops` и MCP-проверкой того же пути обмена)

## Workflow follow-up

- Улучшить поиск по `ops` для вопросов про mTLS (#21, #22 набора качества): README `wallet-mcp`/`wallet-telegram`/`wallet-ai` вытесняют `wallet-infra/infra/env`; кандидаты — учёт `capability` в тексте запроса или формулировка вопросов набора; отдельным change.
- Отдельный change в wallet-infra: `docs-index.sh` собирает `README.md` репозиториев с `openspec/`, пропуская `third_party/`, `node_modules/`, `.worktrees/`, `build/`, `vendor/`; без него вид `ops` в индексе остаётся пустым.
- Архивировать change (`openspec archive docs-ops-kind`) после слияния PR и выкатки на test.
- Отдельный change для админа в Telegram и MCP (там нет `role`): план и развилка в `org/docs/TODO_docs_ops_admin_telegram.md`; создать после выкатки этого change на test.
