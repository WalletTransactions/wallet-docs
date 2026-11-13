#!/usr/bin/env python3
"""Замер качества справки wallet-docs (change docs-answer-quality, задача 1.2).

Гоняет tests/quality/questions.json через POST {DOCS_URL}/api/docs/ask.
Токены — файлами (в аргументах и выводе не светятся):
  DOCS_TOKEN_ANDROID_FILE, DOCS_TOKEN_ADMIN_FILE — JWT с aud ∋ wallet-docs,
  scope ∋ docs:ask, platform=android / role=admin (cookie `session` из веб-кабинета
  живёт ~15 минут: перед замером положить свежий).
DOCS_URL по умолчанию http://localhost:8091 (гейтвей test-контура).

Вердикт: верно — ответ не отказ и в sources есть ожидаемый раздел, либо expect=null
и ответ — отказ. «Ложный отказ» — expect задан, а ответ отказ (виден ли раздел среди
8 фрагментов, API не отдаёт, поэтому считаем по ответу).
Запуск:  python3 -I scripts/quality.py [--no-holdout] [--only holdout]
"""
import json, os, sys, time, urllib.error, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
QUESTIONS = os.path.join(HERE, "..", "tests", "quality", "questions.json")
NOT_FOUND = "В описании системы этого нет."


def token(platform):
    path = os.environ.get("DOCS_TOKEN_%s_FILE" % platform.upper())
    if not path:
        sys.exit("нет DOCS_TOKEN_%s_FILE" % platform.upper())
    with open(os.path.expanduser(path)) as f:
        t = f.read().strip()
    return t[len("session="):] if t.startswith("session=") else t


def ask(url, tok, question):
    req = urllib.request.Request(
        url.rstrip("/") + "/api/docs/ask",
        json.dumps({"question": question}).encode(),
        {"Authorization": "Bearer " + tok, "Content-Type": "application/json"},
    )
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            body = json.loads(r.read())
    except urllib.error.HTTPError as e:
        return {"error": "HTTP %d" % e.code}, time.monotonic() - t0
    return body, time.monotonic() - t0


def main():
    mode = "all"
    if "--no-holdout" in sys.argv:
        mode = "train"
    if "--only" in sys.argv and "holdout" in sys.argv:
        mode = "holdout"
    url = os.environ.get("DOCS_URL", "http://localhost:8091")
    qs = json.load(open(QUESTIONS))["questions"]
    if mode == "train":
        qs = [q for q in qs if not q.get("holdout")]
    elif mode == "holdout":
        qs = [q for q in qs if q.get("holdout")]
    tokens, ok, false_refusals, times = {}, 0, 0, []
    for q in qs:
        tok = tokens.setdefault(q["platform"], token(q["platform"]))
        body, dt = ask(url, tok, q["q"])
        times.append(dt)
        if "error" in body:
            verdict, srcs = "ОШИБКА " + str(body["error"]), []
        else:
            srcs = ["%s/%s" % (s["repo"], s["capability"]) for s in body.get("sources", [])]
            refusal = body.get("answer", "").strip() == NOT_FOUND
            if q["expect"] is None:
                good = refusal
            else:
                good = (not refusal) and q["expect"] in srcs
            if q["expect"] is not None and refusal:
                false_refusals += 1
                verdict = "ЛОЖНЫЙ ОТКАЗ"
            else:
                verdict = "верно" if good else "мимо"
            ok += good
        print("#%-2d %-7s %-13s %5.1fс  %s" % (q["id"], q["platform"], verdict, dt, q["q"]))
        print("      ожидалось: %s; источники: %s" % (q["expect"], ", ".join(srcs) or "—"))
    times.sort()
    p95 = times[min(len(times) - 1, int(len(times) * 0.95))] if times else 0
    print("\nИтого: %d из %d верных; ложных отказов %d; p95 %.1f с" % (ok, len(qs), false_refusals, p95))


if __name__ == "__main__":
    main()
