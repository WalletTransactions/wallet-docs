## Why

На test (2026-11-13) `POST /api/docs/ask` не отвечает: поиск находит 8 фрагментов, а вызов LLM идёт 42,8 с и кончается `empty_answer` (`503`), гейтвей на 30 с отдаёт `502`. Чат-модель в LM Studio — `google/gemma-4-e4b` под id `wallet-parse`, модель с рассуждением. `wallet-docs` не передаёт `reasoning_effort`, поэтому модель тратит `max_tokens: 1024` на рассуждение, `content` остаётся пустым, а ответ занимает десятки секунд. Прямой запрос к LM Studio на малом примере: без параметра 12,5 с и 399 токенов, с `reasoning_effort: "none"` 1,8 с и 52 токена при том же ответе. `wallet-ai` решает то же через `LLM_REASONING_CHAT=none`.

## What Changes

- Новая необязательная переменная `LLM_REASONING_CHAT` (как в `wallet-ai`). Значение по умолчанию `none`: поле `reasoning_effort` уходит в тело `POST /chat/completions`. Значение `off` — поле не передаётся (для моделей и серверов, не принимающих параметр; пустая строка в окружении равна «не задано», как у всех переменных сервиса). Остальные значения `reasoning_effort` OpenAI-совместимого API (`none`, `low`, `medium`, `high`) сервис не проверяет и передаёт как есть.
- Эмбеддинги параметр не получают.
- README: строка в таблице переменных; `openapi.yaml` не меняется (внешний контракт тот же).
- `VERSION` 0.1.0 → 0.1.1.

## Capabilities

### New Capabilities
- `docs-llm-reasoning`: управление рассуждением чат-модели ответа через `LLM_REASONING_CHAT`.

### Modified Capabilities
- нет (основные спеки `docs-answering`/`docs-service-ops` появятся после архива `docs-service`; эта дельта их не затрагивает).

## Impact

- `wallet-docs`: `src/config.{hpp,cpp}`, `src/llm.{hpp,cpp}`, `src/main.cpp`, тесты `test_config.cpp`, `test_llm.cpp`, README, `VERSION`.
- Другие сервисы и клиенты не меняются. Таймаут гейтвея на `/api/docs/*` (30 с) остаётся: ответ без рассуждения укладывается с запасом.
- Проверка на устройстве/контуре: вопрос к `/api/docs/ask` на test после rc-тега `wallet-docs 0.1.1-rc.1`.
