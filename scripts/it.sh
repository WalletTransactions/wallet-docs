#!/usr/bin/env bash
# Интеграционная проверка wallet-docs на настоящем pgvector (задачи 2.1–2.2,
# 3.3, 4.2–4.5 openspec/changes/docs-service): одноразовый Postgres с ролью
# только DML, заглушка google-auth/LM Studio (scripts/it_stub.py), заливка
# реальных спек соседних репозиториев, вопросы с токенами разных клиентов.
#
#   docker build --memory=3g --build-arg BUILD_JOBS=3 -f Containerfile -t wallet-docs:dev .
#   bash scripts/it.sh [образ]          # по умолчанию wallet-docs:dev
#
# Нужны docker, python3 с pyjwt и cryptography, соседние каталоги
# ../android-wallet и ../wallet-gateway. Всё создаётся с префиксом wd-it и
# удаляется в конце.
set -euo pipefail

image="${1:-wallet-docs:dev}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
work="$(mktemp -d)"
net=wd-it
db=wd-it-db
svc=wd-it-svc
svc_port=18089
stub_port=18090
pg_pass=it-postgres
docs_pass=it-docs
index_token=it-index-token
fails=0
stub_pid=

cleanup() {
    docker rm -f "$svc" "$db" > /dev/null 2>&1 || true
    docker network rm "$net" > /dev/null 2>&1 || true
    [[ -n "$stub_pid" ]] && kill "$stub_pid" 2> /dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

ok() { echo "  ok    $*"; }
fail() {
    echo "  FAIL  $*"
    fails=$((fails + 1))
}
check() { # check <описание> <ожидаемое> <фактическое>
    if [[ "$2" == "$3" ]]; then ok "$1"; else fail "$1: ждали '$2', получили '$3'"; fi
}

psql_admin() { docker exec -i "$db" psql -v ON_ERROR_STOP=1 -qtA -U postgres -d wallet_docs "$@"; }

svc_run() { # svc_run <DATABASE_URL> [доп. -e …] — запуск в фоне
    local url="$1"
    shift
    docker rm -f "$svc" > /dev/null 2>&1 || true
    docker run -d --name "$svc" --network "$net" -p "127.0.0.1:$svc_port:8089" \
        --add-host=host.docker.internal:host-gateway \
        -e HOST=0.0.0.0 -e DATABASE_URL="$url" \
        -e AUTH_SERVICE_URL="http://host.docker.internal:$stub_port" "$@" "$image" > /dev/null
}

wait_exit() { # код выхода контейнера (ждём до 15 с)
    for _ in $(seq 30); do
        if [[ "$(docker inspect -f '{{.State.Running}}' "$svc")" == "false" ]]; then
            docker inspect -f '{{.State.ExitCode}}' "$svc"
            return
        fi
        sleep 0.5
    done
    echo running
}

wait_healthy() {
    for _ in $(seq 40); do
        curl -fsS "http://127.0.0.1:$svc_port/healthz" > /dev/null 2>&1 && return 0
        sleep 0.25
    done
    docker logs "$svc" | tail -5
    return 1
}

code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
tok() { python3 -c "import json,sys; print(json.load(open('$work/tokens.json'))[sys.argv[1]])" "$1"; }
ask() { # ask <токен> <вопрос> → тело ответа в $work/ask.json, код — в stdout
    curl -s -o "$work/ask.json" -w '%{http_code}' -X POST "http://127.0.0.1:$svc_port/api/docs/ask" \
        -H "Authorization: Bearer $(tok "$1")" -H 'Content-Type: application/json' \
        --data "$(python3 -c 'import json,sys; print(json.dumps({"question": sys.argv[1]}))' "$2")"
}
jq_py() { python3 -c "import json,sys; d=json.load(open('$work/$1')); print($2)"; }
stub_ctl() { curl -fsS -X POST "http://127.0.0.1:$stub_port/control" -d "$1" > /dev/null; }
stub_calls() { curl -fsS "http://127.0.0.1:$stub_port/calls" | python3 -c "import json,sys; print(json.load(sys.stdin)['$1'])"; }

payload() { # payload <каталог репозитория> → JSON {files:[…]} всех openspec/**/*.md
    python3 - "$1" << 'EOF'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
files = [{"path": str(p.relative_to(root)), "content": p.read_text(encoding="utf-8")}
         for p in sorted(root.glob("openspec/**/*.md"))]
print(json.dumps({"files": files}))
EOF
}
index() { # index <repo> <файл тела> [токен] → тело в $work/index.json, код — в stdout
    curl -s -o "$work/index.json" -w '%{http_code}' -X PUT "http://127.0.0.1:$svc_port/docs/index/$1" \
        -H "X-Index-Token: ${3-$index_token}" -H 'Content-Type: application/json' --data-binary "@$2"
}

echo "== подготовка"
python3 "$here/it_stub.py" "$stub_port" "$work/tokens.json" &
stub_pid=$!
docker network create "$net" > /dev/null
docker run -d --name "$db" --network "$net" -e POSTGRES_PASSWORD="$pg_pass" -e POSTGRES_DB=wallet_docs \
    docker.io/pgvector/pgvector:pg16 > /dev/null
for _ in $(seq 60); do docker exec "$db" pg_isready -U postgres -d wallet_docs > /dev/null 2>&1 && break; sleep 0.5; done
sleep 1
# Как init-db.sh wallet-infra: роль только DML, расширение vector заводит
# администратор, не мигратор и не сервис.
psql_admin << SQL
CREATE ROLE docs LOGIN PASSWORD '$docs_pass';
REVOKE CREATE ON SCHEMA public FROM PUBLIC;
GRANT USAGE ON SCHEMA public TO docs;
CREATE EXTENSION IF NOT EXISTS vector;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT SELECT, INSERT, UPDATE, DELETE ON TABLES TO docs;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT USAGE, SELECT ON SEQUENCES TO docs;
SQL
db_url="postgresql://docs:$docs_pass@$db:5432/wallet_docs"
for _ in $(seq 20); do [[ -s "$work/tokens.json" ]] && break; sleep 0.25; done

echo "== старт без зависимостей (docs-service-ops)"
svc_run "postgresql://docs:$docs_pass@wd-it-nowhere:5432/wallet_docs"
check "база недоступна → код 1" 1 "$(wait_exit)"
docker logs "$svc" 2>&1 | grep -q 'database unavailable: ' && ok "в журнале database unavailable" || fail "нет database unavailable в журнале"

docker rm -f "$svc" > /dev/null
docker run --rm --network "$net" -e DATABASE_URL="$db_url" -e AUTH_SERVICE_URL=http://x "$image" > "$work/nodb.log" 2>&1 || true
grep -q 'миграции не накатаны' "$work/nodb.log" && ok "пустая база → «миграции не накатаны»" || fail "нет «миграции не накатаны»: $(tail -2 "$work/nodb.log")"
docker run --rm --network "$net" "$image" > "$work/noenv.log" 2>&1 && rc=0 || rc=$?
check "без DATABASE_URL → код 1" 1 "$rc"
grep -q 'DATABASE_URL must be set — ' "$work/noenv.log" && ok "текст DATABASE_URL must be set" || fail "нет текста DATABASE_URL must be set"

echo "== миграция (2.1)"
docker run --rm --entrypoint cat "$image" /migrations/000001_init.up.sql > "$work/up.sql"
docker run --rm --entrypoint cat "$image" /migrations/000001_init.down.sql > "$work/down.sql"
psql_admin < "$work/up.sql" && ok "up"
psql_admin < "$work/down.sql" && ok "down"
check "после down таблицы нет" "" "$(psql_admin -c "SELECT to_regclass('public.doc_chunks')")"
psql_admin < "$work/up.sql" && ok "up повторно"
out="$(docker exec -e PGPASSWORD="$docs_pass" "$db" psql -qtA -h 127.0.0.1 -U docs -d wallet_docs -c 'CREATE TABLE t(x int)' 2>&1 || true)"
[[ "$out" == *"permission denied"* ]] && ok "роль docs: CREATE TABLE отклонён" || fail "CREATE TABLE ролью docs: $out"

echo "== сервис"
svc_run "$db_url" -e INDEX_TOKEN="$index_token" -e LLM_URL="http://host.docker.internal:$stub_port/v1" \
    -e LLM_MODEL=it-chat -e EMBED_MODEL=it-embed
wait_healthy && ok "healthz 200" || { fail "сервис не поднялся"; exit 1; }
check "version.service" wallet-docs "$(curl -s "http://127.0.0.1:$svc_port/version" | python3 -c 'import json,sys; print(json.load(sys.stdin)["service"])')"
check "version.version = VERSION" "$(tr -d '[:space:]' < "$root/VERSION")" "$(curl -s "http://127.0.0.1:$svc_port/version" | python3 -c 'import json,sys; print(json.load(sys.stdin)["version"])')"
check "/docs 200" 200 "$(code "http://127.0.0.1:$svc_port/docs")"
check "/openapi.yaml 200" 200 "$(code "http://127.0.0.1:$svc_port/openapi.yaml")"

echo "== заливка (3.3)"
payload "$root/../android-wallet" > "$work/android.json"
payload "$root/../wallet-gateway" > "$work/gateway.json"
check "без токена 401" 401 "$(index android-wallet "$work/android.json" "")"
check "чужой токен 401" 401 "$(index android-wallet "$work/android.json" wrong)"
check "плохое имя 400" 400 "$(index Android_Wallet "$work/android.json")"
start=$(date +%s.%N)
check "android-wallet 200" 200 "$(index android-wallet "$work/android.json")"
echo "        $(cat "$work/index.json") за $(python3 -c "print(round($(date +%s.%N)-$start,2))") с"
a_chunks=$(jq_py index.json 'd["chunks"]')
check "android-wallet: embedded = chunks" "$a_chunks" "$(jq_py index.json 'd["embedded"]')"
check "wallet-gateway 200" 200 "$(index wallet-gateway "$work/gateway.json")"
g_chunks=$(jq_py index.json 'd["chunks"]')
check "строк в базе = chunks" "$((a_chunks + g_chunks))" "$(psql_admin -c 'SELECT count(*) FROM doc_chunks')"
embeds_before=$(stub_calls embed_texts)
check "повтор android-wallet 200" 200 "$(index android-wallet "$work/android.json")"
check "повтор: reused = chunks" "$a_chunks" "$(jq_py index.json 'd["reused"]')"
check "повтор: embedded = 0" 0 "$(jq_py index.json 'd["embedded"]')"
check "повтор: запросов эмбеддингов нет" "$embeds_before" "$(stub_calls embed_texts)"
check "повтор: дублей нет" "$((a_chunks + g_chunks))" "$(psql_admin -c 'SELECT count(*) FROM doc_chunks')"

# Сбой посреди транзакции: DELETE проходит, INSERT отклонён — откат.
psql_admin -c 'REVOKE INSERT ON doc_chunks FROM docs'
check "сбой INSERT → 500" 500 "$(index android-wallet "$work/android.json")"
check "прежний индекс цел" "$a_chunks" "$(psql_admin -c "SELECT count(*) FROM doc_chunks WHERE repo='android-wallet'")"
psql_admin -c 'GRANT INSERT ON doc_chunks TO docs'

# Удалённый файл исчезает, чужой репозиторий не тронут.
python3 - "$work/android.json" "$work/android-less.json" << 'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
d["files"] = d["files"][1:]
json.dump(d, open(sys.argv[2], "w"))
EOF
removed="$(python3 -c "import json; print(json.load(open('$work/android.json'))['files'][0]['path'])")"
check "без одного файла 200" 200 "$(index android-wallet "$work/android-less.json")"
check "чанков удалённого файла нет" 0 "$(psql_admin -c "SELECT count(*) FROM doc_chunks WHERE repo='android-wallet' AND path='$removed'")"
check "wallet-gateway не тронут" "$g_chunks" "$(psql_admin -c "SELECT count(*) FROM doc_chunks WHERE repo='wallet-gateway'")"
check "android-wallet вернуть 200" 200 "$(index android-wallet "$work/android.json")"

echo "== доступ (4.1, 4.2)"
for t in admin android web; do
    check "status $t 200" 200 "$(code -H "Authorization: Bearer $(tok $t)" "http://127.0.0.1:$svc_port/api/docs/status")"
done
check "status other 403" 403 "$(code -H "Authorization: Bearer $(tok other)" "http://127.0.0.1:$svc_port/api/docs/status")"
check "status без aud 401" 401 "$(code -H "Authorization: Bearer $(tok no_aud)" "http://127.0.0.1:$svc_port/api/docs/status")"
check "status без scope 401" 401 "$(code -H "Authorization: Bearer $(tok no_scope)" "http://127.0.0.1:$svc_port/api/docs/status")"
check "status cookie session 200" 200 "$(code -H "Cookie: session=$(tok android)" "http://127.0.0.1:$svc_port/api/docs/status")"

echo "== поиск и ответ (4.3, 4.4)"
check "android: синхронизация 200" 200 "$(ask android 'Как работает синхронизация?')"
check "android: только android-wallet" "android-wallet" "$(jq_py ask.json '",".join(sorted({s["repo"] for s in d["sources"]})) or "-"')"
check "android: маркеров [n] нет" 0 "$(jq_py ask.json 'd["answer"].count("[")')"
check "admin: маршрут /api/mcp/ 200" 200 "$(ask admin 'Как гейтвей маршрутизирует /api/mcp/?')"
check "admin: источник wallet-gateway" "True" "$(jq_py ask.json 'any(s["repo"] == "wallet-gateway" for s in d["sources"])')"
check "android: /api/mcp/ — без wallet-gateway" "False" "$(ask android 'Как гейтвей маршрутизирует /api/mcp/?' > /dev/null; jq_py ask.json 'any(s["repo"] == "wallet-gateway" for s in d["sources"])')"
chat_before=$(stub_calls chat_calls)
check "не по теме 200" 200 "$(ask android 'Какая погода в Москве?')"
check "не по теме: отказ" "В описании системы этого нет." "$(jq_py ask.json 'd["answer"]')"
check "не по теме: LLM не вызвана" "$chat_before" "$(stub_calls chat_calls)"
check "web: синхронизация — пусто (wallet-web не залит)" "В описании системы этого нет." "$(ask web 'Как работает синхронизация?' > /dev/null; jq_py ask.json 'd["answer"]')"
check "other: вопрос 403" 403 "$(ask other 'синхронизация')"

stub_ctl '{"embeddings": false}'
check "эмбеддинги недоступны: вопрос 200 (полнотекст)" 200 "$(ask admin 'синхронизация')"
check "эмбеддинги недоступны: заливка 200" 200 "$(index wallet-gateway "$work/gateway.json")"
stub_ctl '{"embeddings": true}'

stub_ctl '{"llm": false}'
check "LLM недоступна: 503" 503 "$(ask admin 'синхронизация')"
check "LLM недоступна: тело" llm_unavailable "$(jq_py ask.json 'd["error"]')"
check "LLM недоступна: статус 200" 200 "$(code -H "Authorization: Bearer $(tok admin)" "http://127.0.0.1:$svc_port/api/docs/status")"
stub_ctl '{"llm": true}'

echo "== лимит (4.5)"
# Свежий процесс — пустое окно лимита.
docker restart "$svc" > /dev/null
wait_healthy
for i in 1 2 3 4 5 6; do r=$(ask android 'синхронизация'); [[ "$r" == 200 ]] || fail "вопрос $i: $r"; done
check "седьмой 429" 429 "$(ask android 'синхронизация')"
retry="$(curl -s -D - -o /dev/null -X POST "http://127.0.0.1:$svc_port/api/docs/ask" -H "Authorization: Bearer $(tok android)" \
    -H 'Content-Type: application/json' --data '{"question":"x"}' | tr -d '\r' | awk -F': ' 'tolower($1)=="retry-after"{print $2}')"
[[ "$retry" =~ ^[0-9]+$ && "$retry" -ge 1 && "$retry" -le 60 ]] && ok "Retry-After = $retry" || fail "Retry-After: '$retry'"
check "статус под лимитом 200" 200 "$(code -H "Authorization: Bearer $(tok android)" "http://127.0.0.1:$svc_port/api/docs/status")"

echo "== база остановлена после старта"
docker stop "$db" > /dev/null
check "healthz 200" 200 "$(code "http://127.0.0.1:$svc_port/healthz")"
check "status 200" 200 "$(code -H "Authorization: Bearer $(tok admin)" "http://127.0.0.1:$svc_port/api/docs/status")"
check "вопрос 503" 503 "$(ask admin 'синхронизация')"
check "вопрос: database_unavailable" database_unavailable "$(jq_py ask.json 'd["error"]')"

echo
if [[ $fails -eq 0 ]]; then
    echo "it.sh: все проверки прошли"
else
    echo "it.sh: провалено проверок: $fails"
    exit 1
fi
