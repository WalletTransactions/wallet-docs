# wallet-docs

Справочный сервис стека WalletTransactions. Отвечает на вопросы «как это работает» по
OpenSpec-спекам репозиториев. Change: [`openspec/changes/docs-service`](openspec/changes/docs-service/).

```
Android / web ─ JWT (aud wallet-docs) ─> wallet-gateway ─ /api/docs/* ─> wallet-docs ─┬─ JWKS ──────> google-auth
Jenkins ──────── X-Index-Token ─ PUT /docs/index/{repo} (сеть контура) ─>     │        ├─ /embeddings ─> LM Studio
                                                                             │        └─ /chat/completions
                                                                             └─ Postgres (база docs, pgvector)
```

**Необязательный модуль.** Ни один сервис от wallet-docs не зависит. В контуре он
включается compose-профилем `docs` (wallet-infra, change `wallet-docs-assistant`). Сам
сервис обращается только к Postgres, google-auth (JWKS) и LM Studio, wallet-ai не вызывает.

## Доступ и область

- `/api/docs/*` приходит через гейтвей. Токен — JWT google-auth в `Authorization: Bearer`
  или cookie `session`. Сервис проверяет его сам по JWKS
  (`AUTH_SERVICE_URL/.well-known/jwks.json`, ключи грузятся при первом запросе):
  подпись RS256, `exp`, `iss` = `google-auth`, `aud` ∋ `wallet-docs`, `scope` ∋
  `docs:ask`. Если что-то не сходится — `401 {"error":"unauthorized"}`.
- Область документации задаёт только сервер, по `role` и `platform` проверенного токена.
  Таблица — константа в коде (`src/access.cpp`), а не настройка:

  | Токен | Репозитории | Виды |
  |---|---|---|
  | `role: admin` (любая платформа) | все | `spec`, `change`, `archive` |
  | `platform: android` | `android-wallet` | `spec` |
  | `platform: web` | `wallet-web`, `wallet-web-eqdefault` | `spec` |
  | `platform: mcp` (токен MCP, без `role`) | `wallet-web`, `wallet-web-eqdefault`, `wallet-mcp` | `spec` |
  | иначе (`telegram`, без платформы) | — | `403 {"error":"forbidden"}` |

- `/docs/index/{repo}` гейтвей не проксирует. Jenkins ходит напрямую по сети контура с
  `X-Index-Token` = `INDEX_TOKEN`; токены сравниваются за постоянное время.

## Эндпоинты

Полный контракт — [`openapi.yaml`](openapi.yaml), Swagger UI — `GET /docs`.

| Метод и путь | Что делает |
|---|---|
| `GET /api/docs/status` | `200 {"enabled":true}`, если раздел доступен пользователю; `403` при пустой области. От LLM и базы не зависит |
| `POST /api/docs/ask` | `{"question"}` (1–1000 символов) → `{"answer","sources":[{"repo","capability","heading"}]}` |
| `PUT /docs/index/{repo}` | `{"files":[{"path","content"}]}` → `{"repo","chunks","embedded","reused","skipped"}` |
| `GET /healthz`, `/version`, `/metrics`, `/openapi.yaml`, `/docs` | служебные, без аутентификации |

### Вопрос

1. Проверяются токен и область, вопрос валидируется (`400`), затем лимит: не больше 6
   вопросов в минуту на `sub`, иначе `429 {"error":"too_many_requests"}` с `Retry-After`.
2. Вопрос эмбеддится с префиксом `search_query: `. Если эмбеддинги недоступны, поиск идёт
   только полнотекстом.
3. Гибридный поиск в SQL с фильтром области: полнотекст (`russian` + `simple`) и
   векторный (косинусное расстояние ≤ `kMaxVectorDistance` = 0,5), по 20 кандидатов
   каждым способом. Дальше слияние RRF (k = 60), в промпт идут лучшие 8.
4. Кандидатов нет — ответ «В описании системы этого нет.» без вызова LLM.
5. LLM получает фрагменты `[1]…[N]` как данные и отвечает только по ним, ставя номер
   фрагмента после утверждений. В `sources` попадают фрагменты, на которые есть ссылки, в
   порядке первой ссылки и без повторов. Маркеры `[n]` из `answer` удаляются. LLM
   недоступна — `503 {"error":"llm_unavailable"}`, база — `503 {"error":"database_unavailable"}`.

### Заливка индекса

- Индексируются только `openspec/specs/<раздел>/spec.md` (вид `spec`) и `.md` внутри
  `openspec/changes/<имя>/` (вид `change`, а в `archive/` — `archive`; раздел — имя
  change). Остальное (`openspec/project.md`, файлы вне `openspec/`, пути с `..`) считается в
  `skipped`.
- Чанки: в любом `spec.md` — `## Purpose` и каждый `### Requirement:` вместе со
  сценариями. В остальных `.md` — секции `##`. Чанк длиннее 6000 байт режется по абзацам с
  тем же `heading`. Заголовки внутри блоков ``` не учитываются.
- Эмбеддинги считаются пачками по 16 до транзакции (`search_document: ` + заголовок +
  первые 4000 байт текста). Чанк с тем же `path`, `heading` и SHA-256 содержимого берёт
  прежний вектор без запроса. Первая неудачная пачка выключает эмбеддинги до конца
  заливки: остальные чанки сохраняются без вектора и ищутся полнотекстом.
- Замена — одной транзакцией (`DELETE` + один `INSERT … unnest`). Читатели видят либо
  старый индекс, либо новый. Ошибка базы — `500 {"error":"index_failed"}`, прежний индекс
  цел.
- Тело — до 16 МБ (`413`). **Нужен `Content-Type: application/json`**: httplib разбирает
  тело без него как форму с лимитом 8 КБ и отвечает `413` уже на маленьком репозитории.

Время первой заливки (2026-11-13, живая LM Studio, GTX 1070, `text-embedding-nomic-embed-text-v1.5`,
`reused` = 0): `android-wallet` — 802 чанка за 37 с; все 13 репозиториев хоста — 3678 чанков за
166 с суммарно (`docs-index.sh test all`), у всех чанков есть вектор.

```sh
# Заливка одного репозитория (то же будет делать docs-index.sh, wallet-infra change docs-index-upload):
python3 -c 'import json, pathlib, sys; r = pathlib.Path(sys.argv[1]); print(json.dumps({"files": [{"path": str(p.relative_to(r)), "content": p.read_text()} for p in sorted(r.glob("openspec/**/*.md"))]}))' ../android-wallet > /tmp/android.json
curl -sS -X PUT http://127.0.0.1:8089/docs/index/android-wallet \
    -H "X-Index-Token: $INDEX_TOKEN" -H 'Content-Type: application/json' --data-binary @/tmp/android.json

# Вопрос (через гейтвей контура):
curl -sS -X POST http://127.0.0.1:8091/api/docs/ask -H "Authorization: Bearer $JWT" \
    -H 'Content-Type: application/json' -d '{"question":"Как работает синхронизация?"}'
```

## Конфигурация

`.env` в рабочей директории; переменные окружения процесса приоритетнее. Образец —
[`.env.example`](.env.example).

| Переменная | Обязательна | Что делает |
|---|---|---|
| `DATABASE_URL` | да | строка libpq к базе docs; роль только DML |
| `AUTH_SERVICE_URL` | да | google-auth для JWKS |
| `LLM_URL` | нет | корень OpenAI API LM Studio (`http://host.docker.internal:1234/v1`) |
| `LLM_MODEL` | нет | чат-модель ответа; без неё вопрос — `503 llm_unavailable` |
| `EMBED_MODEL` | нет | `text-embedding-nomic-embed-text-v1.5` (768 измерений); без неё поиск только полнотекстом |
| `LLM_API_KEY` | нет | `Authorization: Bearer` к LLM |
| `LLM_TIMEOUT_MS` | нет | дедлайн вызова LLM, по умолчанию 120000 |
| `LLM_REASONING_CHAT` | нет | `reasoning_effort` чат-модели, по умолчанию `none` (как в wallet-ai): модели с рассуждением (gemma-4 в LM Studio) иначе тратят `max_tokens` на «мысли», отвечают пустым `content` и десятки секунд. Значения как у API (`low`, `medium`, `high`) передаются как есть; `off` — поле не передавать. Эмбеддингов не касается |
| `INDEX_TOKEN` | нет | секрет заливки; без него `PUT /docs/index/*` — `503 index_disabled` |
| `DB_POOL_SIZE` | нет | соединений с базой, по умолчанию 4 |
| `HOST`, `PORT` | нет | по умолчанию `127.0.0.1:8089` |
| `LOG_DIR`, `LOG_LEVEL` | нет | как у остальных сервисов |

Если обязательная переменная пуста, сервис пишет `<KEY> must be set — … (см. README.md)` и
завершается с кодом 1.

Старт зависит только от базы. Если она недоступна, сервис пишет `database unavailable: …`
и выходит с кодом 1: перезапуск — политика `restart` compose. Если нет таблицы
`doc_chunks` или расширения `vector`, сервис выходит с «миграции не накатаны: …». DDL сервис
не выполняет. Если база пропала после старта, вопросы и заливка получают `503`/`500`, а
`/healthz` и статус работают дальше.

## База

- `migrations/000001_init.{up,down}.sql` — таблица `doc_chunks`: `repo`, `path`, `kind`,
  `capability`, `heading`, `content`, `content_hash`, `embedding vector(768)`, `tsv`
  (генерируемая колонка `russian || simple`), `indexed_at`. Индексы: `(repo, kind)` и GIN
  по `tsv`. ANN-индекса нет: несколько тысяч чанков сканируются точно.
- Миграции лежат в образе в `/migrations`, их накатывает `migrate.sh <env> docs`
  wallet-infra. Расширение `vector` создаёт `init-db.sh` wallet-infra.
- Индекс в бэкап не входит: он воспроизводится заливкой из Jenkins.

## Метрики

Кроме `http_requests_total` и `http_request_duration_seconds`:

- `docs_answers_total{result}` — `answered`, `not_found`, `bad_request`, `rate_limited`,
  `llm_unavailable`, `database_unavailable`;
- `docs_answers_without_sources_total` — ответы LLM без единой ссылки `[n]`;
- `docs_refusals_total{stage}` — отказы «В описании системы этого нет.»: `no_fragments` — поиск
  ничего не нашёл (LLM не вызывалась), `llm` — отказ дала модель (по ним считается доля ложных
  отказов);
- `docs_auth_rejected_total{reason}` — причины `401`/`403`;
- `docs_index_total{result}` — `ok`, `unauthorized`, `bad_request`, `disabled`, `failed`.

В журнале нет тел запросов (вопросов и текстов спек), есть только счётчики:
`docs_search`, `docs_answered`, `docs_indexed`.

## Качество ответов (замер 2026-11-13, test, `gemma-4-e4b` без рассуждения)

20 вопросов (10 от Android по `android-wallet`, 10 от админа по системе), раздел считался верным,
если ответ по существу совпал со спекой и в `sources` стоит нужный раздел. Итог: 10 из 20 строго
(12, если засчитать верные ответы со ссылкой на архивный change вместо канонической спеки) —
ниже порога 16, поэтому задача 6.3 не закрыта.

- Верно: синхронизация, язык, экспорт, долги, переоценка, icon-sync, первый запуск, ИИ-ввод
  расхода (Android); проверка JWT на гейтвее, выдача MCP-токенов (админ).
- Мимо: «поиск похожих операций» (ответ из `ai-statement-import` вместо `operations-filter`),
  «курсы и валютные пары» (отказ), «хранение блобов иконок» (отказ), «удаление аккаунта» (отказ),
  «доступ к книге в вебе» (отказ), «привязка Telegram» (ответ из baseline `android-wallet`), «push
  в wallet-api» (ответ про другое), «отчёты по балансам» (ответ из архива `capital-assets`).
- Отказ выдаёт LLM по промпту: расстояния до ближайшего чанка у вопросов по теме
  (0,163–0,253) и не по теме (0,231–0,293) пересекаются, порог `kMaxVectorDistance` = 0,5
  ничего не отсекает (design D4). Кандидатов от `archive`/`change` больше, чем от `spec`, и
  вид в ранжировании не учитывается — вероятная причина промахов, проверяется отдельным change.

### Замер после веса вида (2026-11-13, `0.1.2-rc.1` и `0.1.2-rc.2`)

Вес вида: `spec` 1,0, `change`/`archive` 0,6 — сначала только в слиянии RRF (rc.1), затем
и в `ORDER BY` отбора кандидатов в SQL (rc.2). Вопросы заданы заново (другой набор, чем выше:
12 про Android, 8 от админа), поэтому цифры сопоставимы лишь грубо.

- Итог одинаков для обоих rc: верно около 14 из 20 (12 ответов по существу и 2 верных отказа —
  про Telegram у Android и про `docs:ask`, которых нет в индексируемых спеках), ниже порога 16.
- Пять ложных отказов при наличии ответа в спеке: экспорт (`data-export`), переоценка
  (`asset-revaluation`), синхронизация иконок и блобы (`icon-sync`), курсы и пары
  (`currency-pairs`).
- Поиск тут не виноват: замер ранга лучшей спеки по вектору с настоящими эмбеддингами — без
  веса 2–10, с весом в `ORDER BY` везде 1 (кроме `data-export`: 391 → 89, у него всего 2
  коротких чанка). Спека доходит до 8 фрагментов, а LLM (`gemma-4-e4b` без рассуждения) всё
  равно отвечает «этого нет» — причина в промпте или модели, а не в ранжировании.
- Открытых `change`-чанков в индексе 302 против 135 `spec` (в основном `baseline-android-wallet`,
  дублирует спеки); текстовый поиск не связывает «экспортировать» и «экспорт» (стеммер).

## Сборка и проверка

C++-сервисы на хосте собираются только в Docker, по одному за раз и с ограничением памяти:

```sh
docker build --memory=3g --build-arg BUILD_JOBS=3 --target test -f Containerfile .   # юнит-тесты (ctest)
docker build --memory=3g --build-arg BUILD_JOBS=3 -f Containerfile -t wallet-docs:dev .
bash scripts/it.sh wallet-docs:dev   # интеграция: pgvector, заливка, вопросы, отказы
```

`scripts/it.sh` поднимает одноразовый `pgvector/pgvector:pg16` с ролью только DML и
заглушку google-auth/LM Studio (`scripts/it_stub.py`, нужны `pyjwt` и `cryptography`).
Он заливает спеки соседних `../android-wallet` и `../wallet-gateway` и проверяет:
миграцию вверх и вниз, отказ `CREATE TABLE` для роли сервиса, старт без базы и без
миграций, повторную заливку без дублей и запросов эмбеддингов, откат при сбое `INSERT`,
область по токену, отказ «не по теме» без вызова LLM, `503` без LLM и без базы, `429` на
седьмой вопрос. Эмбеддинги заглушки — мешок слов, не модель: качество ответов проверяется
только на живой LM Studio (задача 6.3).

Замер с заглушкой: первая заливка `android-wallet` (≈950 КБ спек, 802 чанка) занимает
≈1,1 с без учёта модели. Время с настоящей LM Studio — задача 3.4.

## Версия и выпуск

`VERSION` — номер, который отдаёт `GET /version`. Тег `vX.Y.Z-rc.N` собирает образ
`ghcr.io/wallettransactions/wallet-docs:X.Y.Z-rc.N` (`.github/workflows/release.yml`),
тег `vX.Y.Z` переносит тот же digest на финальный тег. Схема общая для всех сервисов.

Общий код `third_party/wallet-common/` руками не правится: его обновляет
`bash ../wallet-common/scripts/vendor.sh`.
