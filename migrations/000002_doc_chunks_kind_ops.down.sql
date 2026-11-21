-- Откат вида ops: сначала удаляем его чанки (индекс воспроизводим заливкой),
-- потом возвращаем прежний CHECK из 000001.
DELETE FROM doc_chunks WHERE kind = 'ops';
ALTER TABLE doc_chunks DROP CONSTRAINT doc_chunks_kind_check;
ALTER TABLE doc_chunks
    ADD CONSTRAINT doc_chunks_kind_check CHECK (kind IN ('spec', 'change', 'archive'));
