-- wallet_docs — индекс OpenSpec-спек всех репозиториев (openspec/changes/docs-service).
-- Индекс воспроизводим заливкой из Jenkins (PUT /docs/index/{repo}) и в бэкап
-- не входит.
--
-- Права: роль сервиса (DOCS_POSTGRES_USER) получает только DML через
-- ALTER DEFAULT PRIVILEGES в infra/db/init-db.sh wallet-infra; DDL — мигратор.
-- Расширение vector здесь НЕ создаётся: его заводит init-db.sh (создание
-- расширения требует прав, которых у мигратора может не быть).

-- Один чанк — требование спеки со сценариями, Purpose или секция `##`
-- другого markdown-файла (design D2/D6, спека docs-indexing).
CREATE TABLE doc_chunks (
    id            bigserial   PRIMARY KEY,
    repo          text        NOT NULL,
    path          text        NOT NULL,
    kind          text        NOT NULL CHECK (kind IN ('spec', 'change', 'archive')),
    capability    text        NOT NULL,
    heading       text        NOT NULL,
    content       text        NOT NULL,
    -- SHA-256 (hex) текста, который эмбеддится: по (repo, path, heading,
    -- content_hash) повторная заливка берёт прежний вектор без запроса к LLM.
    content_hash  text        NOT NULL,
    -- nomic-embed-text-v1.5, 768 измерений (D2). NULL — эмбеддинги были
    -- недоступны при заливке: чанк ищется только полнотекстом.
    embedding     vector(768),
    -- Русская морфология + точные идентификаторы и пути (D4). Генерируемая
    -- колонка: сервису не нужно знать формулу, а правка — новой миграцией.
    tsv           tsvector    GENERATED ALWAYS AS (
                      to_tsvector('russian', capability || ' ' || heading || ' ' || content)
                      || to_tsvector('simple', capability || ' ' || heading || ' ' || content)
                  ) STORED,
    indexed_at    timestamptz NOT NULL DEFAULT now()
);

-- Фильтр области (repo, kind) — до ранжирования (D4). ANN-индекса по
-- embedding нет: несколько тысяч чанков сканируются точно (D3).
CREATE INDEX doc_chunks_repo_kind_idx ON doc_chunks (repo, kind);
CREATE INDEX doc_chunks_tsv_idx ON doc_chunks USING gin (tsv);
