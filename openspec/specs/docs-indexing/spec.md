# docs-indexing Specification

## Purpose
Как в сервис попадают OpenSpec-спеки репозитория: приём файлов от Jenkins, разбиение на чанки по структуре спеки, эмбеддинги и атомарная замена индекса репозитория. Индекс воспроизводим и не бэкапится.

## Requirements

### Requirement: Заливка индекса репозитория
`PUT /docs/index/{repo}` SHALL принимать JSON `{"files": [{"path": "<путь внутри репозитория>", "content": "<markdown>"}]}` и заменять в индексе все чанки репозитория `{repo}` одной транзакцией. Имя репозитория SHALL соответствовать `^[a-z0-9][a-z0-9-]{0,63}$`. Успешный ответ — `200` с `{"repo", "chunks", "embedded", "reused", "skipped"}`. Тело SHALL быть не больше 16 МБ, иначе `413`.

#### Scenario: Первая заливка
- **WHEN** Jenkins присылает файлы репозитория `android-wallet`, которого в индексе ещё нет
- **THEN** ответ `200`, `chunks` равно числу полученных чанков, а `POST /api/docs/ask` по этому репозиторию начинает находить их

#### Scenario: Повторная заливка заменяет старое
- **WHEN** репозиторий залит повторно без файла, который был раньше
- **THEN** чанков удалённого файла в индексе больше нет, чанки других репозиториев не тронуты

#### Scenario: Сбой посреди заливки
- **WHEN** заливка прервана ошибкой базы до фиксации транзакции
- **THEN** прежний индекс репозитория остался нетронутым, ответ `500`

#### Scenario: Плохое имя
- **WHEN** приходит `PUT /docs/index/Android Wallet`
- **THEN** ответ `400`

### Requirement: Токен заливки
`PUT /docs/index/{repo}` SHALL требовать заголовок `X-Index-Token`, равный `INDEX_TOKEN` сервиса (сравнение за постоянное время). Без заголовка или с чужим значением ответ — `401`. Если `INDEX_TOKEN` не задан, заливка SHALL отвечать `503`. Путь `/docs/index/*` SHALL NOT проксироваться гейтвеем.

#### Scenario: Нет токена
- **WHEN** приходит `PUT /docs/index/android-wallet` без `X-Index-Token`
- **THEN** ответ `401`, индекс не меняется

#### Scenario: Токен не настроен
- **WHEN** сервис запущен без `INDEX_TOKEN`
- **THEN** заливка отвечает `503`, а `GET /api/docs/status` и вопросы работают по уже имеющемуся индексу

### Requirement: Вид и название чанка из пути
Сервис SHALL определять вид (`kind`) и раздел (`capability`) по пути файла. `openspec/specs/<раздел>/spec.md` — `spec`; `openspec/changes/archive/<имя>/…` — `archive`; `openspec/changes/<имя>/…` — `change`. Для `change` и `archive` разделом считается `<имя>`.

#### Scenario: Спека
- **WHEN** присылается файл `openspec/specs/sync-run/spec.md`
- **THEN** его чанки имеют `kind` `spec` и `capability` `sync-run`

#### Scenario: Архив
- **WHEN** присылается `openspec/changes/archive/2026-11-04-add-wallet-mcp-service/design.md`
- **THEN** чанки имеют `kind` `archive`, а `capability` — `2026-11-04-add-wallet-mcp-service`

### Requirement: Посторонние файлы пропускаются
Индексироваться SHALL только `openspec/specs/<раздел>/spec.md`, `.md` внутри `openspec/changes/` и справочные файлы вида `ops`. Остальное — `.md` в корне `openspec/` (например `project.md`), прочие файлы вне `openspec/`, не `.md`, пути с `..` — SHALL пропускаться и считаться в `skipped` ответа.

#### Scenario: Файл в корне openspec
- **WHEN** присылается `openspec/project.md`
- **THEN** файл не индексируется, `skipped` в ответе увеличен

#### Scenario: Посторонний файл
- **WHEN** присылается `src/main.cpp`, `notes/todo.md` или `../etc/passwd`
- **THEN** файл не индексируется, `skipped` в ответе увеличен

### Requirement: Чанкинг по структуре спеки
Файл `spec.md` SHALL делиться так: `## Purpose` — отдельный чанк; каждый `### Requirement:` вместе со всеми его `#### Scenario:` — отдельный чанк. Остальные markdown-файлы SHALL делиться по заголовкам `##`. Чанк длиннее 6000 символов SHALL разбиваться по абзацам. Каждый чанк хранит `heading` (название требования или секции).

#### Scenario: Требование со сценариями
- **WHEN** в спеке три требования по два сценария
- **THEN** получаются три чанка требований и один чанк Purpose, сценарии лежат в чанке своего требования

#### Scenario: Длинная секция
- **WHEN** секция `##` в `design.md` длиннее 6000 символов
- **THEN** она разбита на несколько чанков с одним и тем же `heading`

### Requirement: Эмбеддинги и повторное использование
Сервис SHALL считать эмбеддинг чанка запросом `POST {LLM_URL}/embeddings` моделью `EMBED_MODEL`, передавая текст с префиксом `search_document: `. Чанк с тем же `repo`, `path`, `heading` и хэшем содержимого, что в прежнем индексе, SHALL использовать прежний эмбеддинг без запроса. Если эмбеддинги недоступны, чанк SHALL сохраняться без вектора, а заливка — завершаться `200` с `embedded: 0`.

#### Scenario: Повторная заливка без изменений
- **WHEN** репозиторий залит второй раз тем же содержимым
- **THEN** `reused` равно числу чанков, `embedded` равно 0, запросов к `/embeddings` нет

#### Scenario: LM Studio недоступна
- **WHEN** заливка идёт при недоступном `/embeddings`
- **THEN** ответ `200`, `embedded` 0, чанки доступны полнотекстовому поиску

#### Scenario: Изменилась одна спека
- **WHEN** изменён один файл из ста
- **THEN** новые эмбеддинги считаются только для чанков этого файла

### Requirement: Вид ops — справка по развёртыванию
Файлы с именем ровно `README.md` на любой глубине репозитория SHALL получать вид `ops`. Разделом (`capability`) служит каталог файла: `README.md` в корне — `README`, `infra/env/README.md` — `infra/env`. Планы `docs/TODO_*.md` и прочие `.md` вне `openspec/` в `ops` не входят. Правила `openspec/` имеют приоритет: `README.md` внутри `openspec/` вида `ops` не получает.

#### Scenario: Корневой README
- **WHEN** присылается `README.md`
- **THEN** его чанки имеют `kind` `ops` и `capability` `README`

#### Scenario: Вложенный README
- **WHEN** присылается `infra/env/README.md`
- **THEN** его чанки имеют `kind` `ops` и `capability` `infra/env`

#### Scenario: Файл плана
- **WHEN** присылается `docs/TODO_service_auth_tls.md`
- **THEN** файл не индексируется, `skipped` в ответе увеличен

#### Scenario: Другое имя файла
- **WHEN** присылается `infra/env/NOTES.md` или `readme.txt`
- **THEN** файл не индексируется, `skipped` в ответе увеличен

### Requirement: Служебные каталоги не индексируются
Файлы вида `ops` внутри каталогов `third_party/`, `node_modules/`, `.worktrees/`, `build/` и `vendor/` SHALL пропускаться и считаться в `skipped`, даже если клиент их прислал.

#### Scenario: README вендорной библиотеки
- **WHEN** присылается `third_party/wallet-common/README.md`
- **THEN** файл не индексируется, `skipped` в ответе увеличен

#### Scenario: README внутри openspec
- **WHEN** присылается `openspec/specs/sync-run/README.md`
- **THEN** файл не индексируется как `ops`, `skipped` в ответе увеличен

### Requirement: Чанкинг справочных файлов
Файлы вида `ops` SHALL делиться как прочие `.md`: по заголовкам `##`, текст до первого `##` — чанк с заголовком `# …` или именем файла, чанк длиннее 6000 символов — по абзацам. Блоки кода SHALL не считаться заголовками.

#### Scenario: README с секциями
- **WHEN** в `README.md` три секции `##` и вводный абзац
- **THEN** получаются четыре чанка вида `ops`, у вводного `heading` — заголовок `# …`
