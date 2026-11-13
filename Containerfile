# syntax=docker/dockerfile:1
#
# Multi-stage build wallet-docs — тот же шаблон, что у wallet-mcp и
# wallet-ai: deps -> build (образ), deps -> test (юнит-тесты, `--target test`),
# runtime. libpq — своя база docs; OpenSSL — RS256 входящего JWT и SHA-256.
# curl в рантайме — healthcheck compose и docs-index.sh wallet-infra.
# Миграции лежат в образе (/migrations): их накатывает migrate.sh контура.
#
# Сборка на хосте — по одному C++-сервису за раз и с ограничением памяти:
#   docker build --memory=3g --build-arg BUILD_JOBS=3 -f Containerfile -t wallet-docs:dev .

FROM debian:bookworm-slim AS deps

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libpq-dev \
        libssl-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY VERSION ./
COPY openapi.yaml ./
COPY src/ src/
COPY third_party/ third_party/

FROM deps AS build
# GIT_COMMIT — снаружи (`--build-arg`): у сборки в контейнере нет .git.
ARG GIT_COMMIT=unknown
# APP_BUILD — тег образа (`0.1.0-rc.1`), его передаёт release.yml; пусто —
# CMake подставит `<VERSION>-dev` (GET /version, поле build).
ARG APP_BUILD=
ARG BUILD_JOBS=3
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DWALLET_DOCS_BUILD_TESTS=OFF -DGIT_COMMIT="${GIT_COMMIT}" -DAPP_BUILD="${APP_BUILD}" \
    && cmake --build build --config Release -j"${BUILD_JOBS}"

FROM deps AS test
COPY tests/ tests/
ARG BUILD_JOBS=3
RUN cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug \
    && cmake --build build-tests --target wallet_docs_tests -j"${BUILD_JOBS}" \
    && ctest --test-dir build-tests --output-on-failure

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libpq5 \
        libssl3 \
        curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /app --shell /usr/sbin/nologin appuser

WORKDIR /app
COPY --from=build /src/build/wallet_docs /app/wallet_docs
# openapi.yaml + Swagger UI читаются с диска при GET /openapi.yaml, /docs.
COPY openapi.yaml /app/openapi.yaml
COPY third_party/swagger-ui/ /app/vendor/swagger-ui/
# Схема базы docs — для migrate.sh контура (wallet-infra, change
# wallet-docs-assistant, D6): мигратор берёт /migrations из образа.
COPY migrations/ /migrations/

# LOG_DIR не задан -> логи в stdout/stderr.
USER appuser
EXPOSE 8089

ENTRYPOINT ["/app/wallet_docs"]
