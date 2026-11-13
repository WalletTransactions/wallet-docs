#include "rate_limit.hpp"

#include <iterator>

namespace ratelimit {

namespace {
// Раз в столько вызовов выбрасываем пользователей с пустым окном, чтобы
// карта не росла бесконечно.
constexpr unsigned kSweepEvery = 256;
} // namespace

Clock steady_clock() {
    return [] { return std::chrono::steady_clock::now(); };
}

Limiter::Limiter(std::size_t limit, std::chrono::seconds window, Clock clock)
    : limit_(limit), window_(window), clock_(std::move(clock)) {}

std::optional<int> Limiter::acquire(const std::string& key) {
    auto now = clock_();
    std::lock_guard<std::mutex> lock(mu_);
    auto drop_old = [&](std::deque<std::chrono::steady_clock::time_point>& q) {
        while (!q.empty() && now - q.front() >= window_) q.pop_front();
    };
    if (++calls_ % kSweepEvery == 0) {
        for (auto it = hits_.begin(); it != hits_.end();) {
            drop_old(it->second);
            it = it->second.empty() ? hits_.erase(it) : std::next(it);
        }
    }
    auto& q = hits_[key];
    drop_old(q);
    if (q.size() >= limit_) {
        auto wait = window_ - (now - q.front());
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(wait).count();
        // Округление вверх: ровно через secs слот может ещё не освободиться.
        if (std::chrono::duration_cast<std::chrono::seconds>(wait) < wait) ++secs;
        return static_cast<int>(secs < 1 ? 1 : secs);
    }
    q.push_back(now);
    return std::nullopt;
}

} // namespace ratelimit
