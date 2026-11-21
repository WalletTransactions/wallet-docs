-- Вид ops — README по развёртыванию (change docs-ops-kind, спека docs-indexing).
-- Расширяет CHECK на doc_chunks.kind четвёртым значением; данные не меняются.
-- Имя ограничения — по умолчанию Postgres для колонного CHECK из 000001.
ALTER TABLE doc_chunks DROP CONSTRAINT doc_chunks_kind_check;
ALTER TABLE doc_chunks
    ADD CONSTRAINT doc_chunks_kind_check CHECK (kind IN ('spec', 'change', 'archive', 'ops'));
