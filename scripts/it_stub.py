#!/usr/bin/env python3
"""Заглушка google-auth (JWKS) и LM Studio (/v1/embeddings, /v1/chat/completions)
для scripts/it.sh. Только для локальной проверки, в образ не входит.

Эмбеддинг — мешок «стемов» (первые 5 букв слова) в 768 корзинах,
нормированный: тексты с общими словами близки, без общих — ортогональны.
Это не модель, а детерминированная замена, чтобы проверить SQL и пороги.

  python3 it_stub.py <port> <tokens.json>

tokens.json получает подписанные JWT: admin, android, web, other, no_aud.
Управление: POST /control {"llm": false} — LLM отвечает 503;
{"embeddings": false} — /embeddings отвечает 503. GET /calls — счётчики.
"""
import base64
import hashlib
import json
import math
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import jwt
from cryptography.hazmat.primitives.asymmetric import rsa

DIM = 768
KEY = rsa.generate_private_key(public_exponent=65537, key_size=2048)
KID = "it-key"
STATE = {"llm": True, "embeddings": True, "chat_calls": 0, "embed_calls": 0, "embed_texts": 0}


def b64url_uint(n):
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode()


def jwks():
    pub = KEY.public_key().public_numbers()
    return {"keys": [{"kty": "RSA", "kid": KID, "alg": "RS256", "use": "sig",
                      "n": b64url_uint(pub.n), "e": b64url_uint(pub.e)}]}


def token(role, platform, aud=None, scope=None):
    now = int(time.time())
    claims = {"iss": "google-auth", "sub": "it-" + role + "-" + (platform or "none"), "role": role,
              "aud": aud if aud is not None else ["wallet-api", "wallet-report", "wallet-docs"],
              "scope": scope if scope is not None else "reports:read docs:ask",
              "iat": now, "exp": now + 3600}
    if platform:
        claims["platform"] = platform
        claims["device_id"] = "it-device"
    return jwt.encode(claims, KEY, algorithm="RS256", headers={"kid": KID})


WORD = re.compile(r"[0-9a-zа-яё_/.-]+", re.IGNORECASE)


def embed(text):
    v = [0.0] * DIM
    for w in WORD.findall(text.lower()):
        if w in ("search_document", "search_query") or len(w) < 3:
            continue
        stem = w[:5]
        h = int(hashlib.sha256(stem.encode()).hexdigest(), 16)
        v[h % DIM] += 1.0
    norm = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / norm for x in v]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send_json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length", "0"))
        return json.loads(self.rfile.read(n) or b"{}")

    def do_GET(self):
        if self.path == "/.well-known/jwks.json":
            return self.send_json(200, jwks())
        if self.path == "/calls":
            return self.send_json(200, STATE)
        self.send_json(404, {})

    def do_POST(self):
        if self.path == "/control":
            STATE.update(self.body())
            return self.send_json(200, STATE)
        if self.path == "/v1/embeddings":
            req = self.body()
            STATE["embed_calls"] += 1
            if not STATE["embeddings"]:
                return self.send_json(503, {"error": "down"})
            texts = req["input"]
            STATE["embed_texts"] += len(texts)
            return self.send_json(200, {"data": [{"index": i, "embedding": embed(t)} for i, t in enumerate(texts)]})
        if self.path == "/v1/chat/completions":
            req = self.body()
            STATE["chat_calls"] += 1
            if not STATE["llm"]:
                return self.send_json(503, {"error": "down"})
            user = req["messages"][1]["content"]
            n = user.count("<<<ФРАГМЕНТ [")
            # Ссылки на последний и первый фрагменты — порядок первой ссылки.
            answer = "Ответ по документации [%d]. Подробнее [1]." % n
            return self.send_json(200, {"choices": [{"message": {"role": "assistant", "content": answer}}]})
        self.send_json(404, {})


def main():
    port = int(sys.argv[1])
    with open(sys.argv[2], "w") as f:
        json.dump({"admin": token("admin", "web"), "android": token("user", "android"),
                   "web": token("user", "web"), "other": token("user", "other"),
                   "no_aud": token("user", "android", aud=["wallet-api"]),
                   "no_scope": token("user", "android", scope="reports:read")}, f)
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
