// Лимит вопросов на пользователя (design D9, спека docs-answering
// «Ограничение частоты»): скользящее окно в памяти процесса — сервис один.
#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ratelimit {

using Clock = std::function<std::chrono::steady_clock::time_point()>;
Clock steady_clock();

class Limiter {
public:
    Limiter(std::size_t limit, std::chrono::seconds window, Clock clock = steady_clock());

    // nullopt — вопрос принят и учтён; иначе секунды до освобождения слота
    // (Retry-After, не меньше 1).
    std::optional<int> acquire(const std::string& key);

private:
    std::size_t limit_;
    std::chrono::seconds window_;
    Clock clock_;
    std::mutex mu_;
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> hits_;
    unsigned calls_ = 0;
};

} // namespace ratelimit
