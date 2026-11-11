# Spec Delta

## Purpose

Запуск и служебное поведение wallet-docs как необязательного модуля контура: конфигурация, работа при недоступных зависимостях и стандартные ручки сервиса.

## ADDED Requirements

### Requirement: Конфигурация
Сервис SHALL читать `.env` из рабочей директории (переменные окружения процесса приоритетнее) и требовать `DATABASE_URL` и `AUTH_SERVICE_URL`. `LLM_URL`, `LLM_MODEL`, `EMBED_MODEL` и `INDEX_TOKEN` необязательны: без них вопросы отвечают `503` (`llm_unavailable`), эмбеддинги не считаются, заливка отвечает `503`. Если обязательная переменная пуста, сервис пишет `<KEY> must be set — <подсказка> (см. README.md)` и завершается с кодом `1`.

#### Scenario: Нет DATABASE_URL
- **WHEN** сервис запускается без `DATABASE_URL`
- **THEN** в журнале `DATABASE_URL must be set — …`, код выхода 1

#### Scenario: Нет LLM
- **WHEN** сервис запущен без `LLM_URL`
- **THEN** он стартует, `GET /api/docs/status` отвечает `200` (по токену), вопрос — `503`

### Requirement: Схема базы проверяется, не создаётся
Сервис SHALL при старте проверять, что миграции накатаны (таблица `doc_chunks` и расширение `vector`), и завершаться с понятной ошибкой, если нет. DDL SHALL NOT выполняться сервисом: схему создаёт мигратор из `/migrations` образа. `doc_chunks` SHALL содержать `repo`, `path`, `kind`, `capability`, `heading`, `content`, `content_hash`, `embedding vector(768)`, `tsv`, `indexed_at`.

#### Scenario: Миграций нет
- **WHEN** сервис запускается на пустой базе
- **THEN** он завершается с ошибкой «миграции не накатаны», код 1

#### Scenario: Сервис не создаёт таблиц
- **WHEN** роль сервиса пытается выполнить `CREATE TABLE`
- **THEN** база отказывает (роль только DML), в работе сервиса это не нужно

### Requirement: Служебные ручки
Сервис SHALL отдавать без аутентификации `GET /healthz` (жив ли процесс, без обращения к базе и LLM), `GET /version` (`service`, `version`, `build`, `git_commit`, `started_at`), `GET /metrics`, `GET /openapi.yaml` и `GET /docs` (Swagger UI без внешних запросов).

#### Scenario: Health при остановленной базе
- **WHEN** Postgres недоступен
- **THEN** `GET /healthz` отвечает `200`, а вопрос — `503`

#### Scenario: Версия
- **WHEN** выполняется `GET /version`
- **THEN** ответ содержит `service` = `wallet-docs`, `version` из файла `VERSION`

### Requirement: Независимость от остальных сервисов
Сервис SHALL обращаться только к Postgres, google-auth (JWKS) и LLM-хосту. Вызовы wallet-ai, wallet-api, wallet-report и wallet-mcp SHALL NOT выполняться, а их остановка SHALL NOT влиять на работу сервиса.

#### Scenario: Остановлен wallet-ai
- **WHEN** wallet-ai остановлен
- **THEN** статус и вопросы wallet-docs работают без изменений
