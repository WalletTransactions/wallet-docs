# Spec Delta

## MODIFIED Requirements

### Requirement: Посторонние файлы пропускаются
Индексироваться SHALL только `openspec/specs/<раздел>/spec.md`, `.md` внутри `openspec/changes/` и справочные файлы вида `ops`. Остальное — `.md` в корне `openspec/` (например `project.md`), прочие файлы вне `openspec/`, не `.md`, пути с `..` — SHALL пропускаться и считаться в `skipped` ответа.

#### Scenario: Файл в корне openspec
- **WHEN** присылается `openspec/project.md`
- **THEN** файл не индексируется, `skipped` в ответе увеличен

#### Scenario: Посторонний файл
- **WHEN** присылается `src/main.cpp`, `notes/todo.md` или `../etc/passwd`
- **THEN** файл не индексируется, `skipped` в ответе увеличен

## ADDED Requirements

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
