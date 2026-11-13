add_executable(wallet_docs_tests
    test_main.cpp
    test_access.cpp
    test_answer.cpp
    test_chunking.cpp
    test_config.cpp
    test_http.cpp
    test_indexer.cpp
    test_llm.cpp
    test_pg_recover.cpp
    test_rate_limit.cpp
    test_search.cpp
)
target_include_directories(wallet_docs_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Реальные спеки wallet-gateway (копии в fixtures/) — для тестов чанкинга.
target_compile_definitions(wallet_docs_tests PRIVATE
    WALLET_DOCS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(wallet_docs_tests PRIVATE wallet_docs_core)

add_test(NAME wallet_docs_unit_tests COMMAND wallet_docs_tests)
