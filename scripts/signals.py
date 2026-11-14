#!/usr/bin/env python3
"""Замер сигналов «не по теме» до LLM (change docs-answer-quality, задача 3.1).

Для каждого вопроса из tests/quality/questions.json (по теме) и offtopic.json
(не по теме) считает те же величины, что видит поиск сервиса: косинусное
расстояние первого и второго векторных кандидатов (с весом вида, как в
kVectorSql), разрыв между ними и наибольший ts_rank текстового кандидата.
Область доступа — как в access.cpp: android → android-wallet/spec, admin — всё.

Эмбеддинг — тот же вызов, что в сервисе (префикс `search_query: `). SQL идёт в
БД docs через `docker exec <DB_CONTAINER> psql`. Секреты — из файла окружения
сервиса (--env-file), в вывод не попадают.

  python3 -I scripts/signals.py --env-file ENV [--db-container wallet-test-db-1]
                                [--llm-url http://127.0.0.1:1234/v1]
"""
import argparse, json, os, subprocess, sys, urllib.request
from urllib.parse import urlsplit, urlunsplit

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.join(HERE, "..", "tests", "quality")


def load_env(path):
    env = {}
    for line in open(path):
        if "=" in line and not line.startswith("#"):
            k, v = line.rstrip("\n").split("=", 1)
            env[k] = v
    return env


def embed(llm_url, key, model, text):
    req = urllib.request.Request(
        llm_url.rstrip("/") + "/embeddings",
        json.dumps({"model": model, "input": ["search_query: " + text]}).encode(),
        {"Authorization": "Bearer " + key, "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read())["data"][0]["embedding"]


def psql(container, url, sql):
    p = urlsplit(url)
    host_fixed = urlunsplit((p.scheme, p.netloc.rsplit("@", 1)[0] + "@localhost:5432", p.path, "", ""))
    out = subprocess.run(
        ["docker", "exec", "-i", container, "psql", host_fixed, "-At", "-F", "|", "-c", sql],
        capture_output=True, text=True,
    )
    if out.returncode != 0:
        sys.exit("psql: " + out.stderr.strip().splitlines()[-1])
    return [l.split("|") for l in out.stdout.strip().splitlines() if l]


def scope_sql(platform):
    if platform == "android":
        return "d.repo = 'android-wallet' AND d.kind = 'spec'"
    return "d.kind IN ('spec','change','archive')"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--env-file", required=True)
    ap.add_argument("--db-container", default="wallet-test-db-1")
    ap.add_argument("--llm-url", default="http://127.0.0.1:1234/v1")
    a = ap.parse_args()
    env = load_env(a.env_file)
    sets = [("тема", json.load(open(os.path.join(TESTS, "questions.json")))["questions"]),
            ("НЕ ТЕМА", json.load(open(os.path.join(TESTS, "offtopic.json")))["questions"])]
    print("%-8s %-4s %7s %7s %7s %8s  %s" % ("набор", "id", "d1", "d2", "разрыв", "ts_rank", "вопрос"))
    for label, qs in sets:
        for q in qs:
            vec = "[" + ",".join(repr(x) for x in embed(a.llm_url, env["LLM_API_KEY"], env["EMBED_MODEL"], q["q"])) + "]"
            sc = scope_sql(q["platform"])
            dist = psql(a.db_container, env["DATABASE_URL"],
                        "SELECT (d.embedding <=> '%s'::vector) / (CASE WHEN d.kind='spec' THEN 1.0 ELSE 0.6 END) AS w "
                        "FROM doc_chunks d WHERE d.embedding IS NOT NULL AND %s ORDER BY w LIMIT 2" % (vec, sc))
            qt = q["q"].replace("'", "''")
            rank = psql(a.db_container, env["DATABASE_URL"],
                        "WITH q AS (SELECT to_tsquery('simple', coalesce(string_agg(quote_literal(l),' | '),'')) AS tq FROM ("
                        "SELECT unnest(tsvector_to_array(to_tsvector('russian','%s'))) AS l UNION "
                        "SELECT l FROM unnest(tsvector_to_array(to_tsvector('simple','%s'))) AS l WHERE l ~ '[a-z0-9]') x) "
                        "SELECT coalesce(max(ts_rank(d.tsv,q.tq)),0) FROM doc_chunks d, q WHERE d.tsv @@ q.tq AND %s"
                        % (qt, qt, sc))
            d1, d2 = float(dist[0][0]), float(dist[1][0])
            print("%-8s %-4d %7.3f %7.3f %7.3f %8.4f  %s" % (label, q["id"], d1, d2, d2 - d1, float(rank[0][0]), q["q"]))


if __name__ == "__main__":
    main()
