cmake_minimum_required(VERSION 3.16)
project(wallet_docs CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Общий код сервисов (util/) — копия репозитория wallet-common, обновляется
# его scripts/vendor.sh; руками не править. expense/ и clients/ лежат рядом
# (vendor.sh копирует всё), но сервису не нужны и не собираются.
set(WALLET_COMMON_DIR ${CMAKE_SOURCE_DIR}/third_party/wallet-common)

# OpenSSL — проверка RS256 входящего JWT и SHA-256 (хэш чанка, X-Index-Token).
# PostgreSQL — своя база docs (doc_chunks, pgvector).
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(Threads REQUIRED)

# Версия — тот же приём, что у остальных сервисов: номер из VERSION, хэш —
# из -DGIT_COMMIT (Containerfile) или из git при нативной сборке, сборка —
# тег образа из -DAPP_BUILD (release.yml), иначе `<VERSION>-dev`.
file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION APP_VERSION_LINES LIMIT_COUNT 1)
list(GET APP_VERSION_LINES 0 APP_VERSION)
string(STRIP "${APP_VERSION}" APP_VERSION)

if(NOT DEFINED APP_BUILD OR "${APP_BUILD}" STREQUAL "")
    set(APP_BUILD "${APP_VERSION}-dev")
endif()

if(NOT DEFINED GIT_COMMIT OR "${GIT_COMMIT}" STREQUAL "")
    find_program(GIT_EXECUTABLE git)
    set(GIT_COMMIT "unknown")
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty --abbrev=8
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE GIT_DESCRIBE_RESULT
            ERROR_QUIET
        )
        if(NOT GIT_DESCRIBE_RESULT EQUAL 0 OR "${GIT_COMMIT}" STREQUAL "")
            set(GIT_COMMIT "unknown")
        endif()
    endif()
endif()
message(STATUS "wallet_docs version: ${APP_VERSION}, build ${APP_BUILD} (${GIT_COMMIT})")

configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/version.hpp
    @ONLY
)

# Всё, кроме main.cpp, — библиотека: её же линкуют юнит-тесты.
add_library(wallet_docs_core STATIC
    src/access.cpp
    src/answer.cpp
    src/chunking.cpp
    src/config.cpp
    src/db.cpp
    src/http_api.cpp
    src/indexer.cpp
    src/llm.cpp
    src/pg_store.cpp
    src/rate_limit.cpp
    src/search.cpp
    src/service_endpoints.cpp
    ${WALLET_COMMON_DIR}/util/base64.cpp
    ${WALLET_COMMON_DIR}/util/crypto.cpp
    ${WALLET_COMMON_DIR}/util/dotenv.cpp
    ${WALLET_COMMON_DIR}/util/jwks.cpp
    ${WALLET_COMMON_DIR}/util/jwt.cpp
    ${WALLET_COMMON_DIR}/util/jwt_claims.cpp
    ${WALLET_COMMON_DIR}/util/log.cpp
    ${WALLET_COMMON_DIR}/util/metrics.cpp
)
target_include_directories(wallet_docs_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${WALLET_COMMON_DIR}
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(wallet_docs_core PUBLIC
    OpenSSL::Crypto
    PostgreSQL::PostgreSQL
    Threads::Threads
)

add_executable(wallet_docs src/main.cpp)
target_link_libraries(wallet_docs PRIVATE wallet_docs_core)

add_custom_command(TARGET wallet_docs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/openapi.yaml
            $<TARGET_FILE_DIR:wallet_docs>/openapi.yaml
    COMMENT "Копирую openapi.yaml рядом с wallet_docs (GET /openapi.yaml, /docs)"
)
add_custom_command(TARGET wallet_docs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/third_party/swagger-ui
            $<TARGET_FILE_DIR:wallet_docs>/vendor/swagger-ui
    COMMENT "Копирую вендоренный Swagger UI рядом с wallet_docs (GET /docs)"
)

# Юнит-тесты — без сети и без базы: чистые функции, фейковые хранилище, LLM и
# JWKS, HTTP-ручки на локальном httplib-сервере. В образе — стадия `test`:
#   docker build --target test -f Containerfile .
# Интеграционная проверка на настоящем pgvector — scripts/it.sh.
option(WALLET_DOCS_BUILD_TESTS "Собирать юнит-тесты wallet_docs_tests" ON)
if(WALLET_DOCS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
