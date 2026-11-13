#include <thread>

#include "httplib.h"
#include "llm.hpp"
#include "test_framework.hpp"

namespace {

nlohmann::json embedding_response(std::size_t n, float value = 0.1f) {
    nlohmann::json data = nlohmann::json::array();
    for (std::size_t i = 0; i < n; ++i) {
        data.push_back({{"object", "embedding"}, {"index", i}, {"embedding", std::vector<float>(llm::kEmbeddingDim, value + i)}});
    }
    return {{"object", "list"}, {"data", data}};
}

} // namespace

TEST(split_base_url_cases) {
    auto b = llm::split_base_url("http://host:1234/v1/");
    CHECK_EQ(b.origin, std::string("http://host:1234"));
    CHECK_EQ(b.path, std::string("/v1"));
    CHECK(llm::split_base_url("nonsense").origin.empty());
}

TEST(parse_embeddings_ok_and_order) {
    auto j = embedding_response(2);
    std::swap(j["data"][0], j["data"][1]); // index задаёт порядок
    auto r = llm::parse_embeddings(200, j.dump(), 2);
    REQUIRE(r.ok);
    CHECK_EQ(r.vectors.size(), 2u);
    CHECK(r.vectors[0][0] < r.vectors[1][0]);
}

TEST(parse_embeddings_rejects) {
    CHECK(!llm::parse_embeddings(500, "{}", 1).ok);
    CHECK(!llm::parse_embeddings(200, "not json", 1).ok);
    CHECK(!llm::parse_embeddings(200, embedding_response(1).dump(), 2).ok);
    auto wrong_dim = nlohmann::json{{"data", {{{"index", 0}, {"embedding", {1.0, 2.0}}}}}};
    auto r = llm::parse_embeddings(200, wrong_dim.dump(), 1);
    CHECK(!r.ok);
    CHECK_EQ(r.error, std::string("dimension_2"));
}

TEST(parse_chat_and_think) {
    auto body = nlohmann::json{{"choices", {{{"message", {{"content", "<think>хм</think>\n Ответ [1]. "}}}}}}};
    auto r = llm::parse_chat(200, body.dump());
    REQUIRE(r.ok);
    CHECK_EQ(r.content, std::string("Ответ [1]."));
    CHECK(!llm::parse_chat(404, "{}").ok);
    CHECK(!llm::parse_chat(200, R"({"choices":[]})").ok);
}

TEST(chat_body_shape) {
    auto b = llm::chat_body("m", "sys", "user");
    CHECK_EQ(b["model"], "m");
    CHECK_EQ(b["messages"][0]["role"], "system");
    CHECK_EQ(b["messages"][1]["content"], "user");
}

// Клиент эмбеддингов против заглушки LM Studio: путь, модель, префиксы.
TEST(http_embedder_against_stub) {
    httplib::Server stub;
    nlohmann::json seen;
    std::string auth;
    stub.Post("/v1/embeddings", [&](const httplib::Request& req, httplib::Response& res) {
        seen = nlohmann::json::parse(req.body);
        auth = req.get_header_value("Authorization");
        res.set_content(embedding_response(seen["input"].size()).dump(), "application/json");
    });
    int port = stub.bind_to_any_port("127.0.0.1");
    std::thread t([&] { stub.listen_after_bind(); });
    stub.wait_until_ready();

    auto e = llm::make_http_embedder({"http://127.0.0.1:" + std::to_string(port) + "/v1", "nomic", "k", 5000});
    auto r = e->embed({std::string(llm::kDocumentPrefix) + "текст", std::string(llm::kQueryPrefix) + "вопрос"});
    stub.stop();
    t.join();

    REQUIRE(r.ok);
    CHECK_EQ(r.vectors.size(), 2u);
    CHECK_EQ(r.vectors[0].size(), llm::kEmbeddingDim);
    CHECK_EQ(seen["model"], "nomic");
    CHECK_EQ(seen["input"][0], "search_document: текст");
    CHECK_EQ(seen["input"][1], "search_query: вопрос");
    CHECK_EQ(auth, std::string("Bearer k"));
}

TEST(http_embedder_unavailable) {
    // Порт, на котором никто не слушает.
    httplib::Server probe;
    int port = probe.bind_to_any_port("127.0.0.1");
    probe.stop();
    auto e = llm::make_http_embedder({"http://127.0.0.1:" + std::to_string(port) + "/v1", "nomic", "", 2000});
    auto r = e->embed({"x"});
    CHECK(!r.ok);
    CHECK(!r.error.empty());
}
