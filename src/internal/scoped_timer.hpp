#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>

namespace axvp::internal {

class ScopedTimer final {
  public:
    using clock = std::chrono::steady_clock;

    explicit ScopedTimer(std::atomic<std::uint32_t> &sink) noexcept
        : sink_(&sink), start_(clock::now()) {}

    ~ScopedTimer() noexcept { stop(); }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;
    ScopedTimer(ScopedTimer &&) = delete;
    ScopedTimer &operator=(ScopedTimer &&) = delete;

    void stop() noexcept {
        if (!active_ || sink_ == nullptr) {
            return;
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(clock::now() -
                                                                  start_)
                .count();
        constexpr auto max_value = static_cast<std::chrono::microseconds::rep>(
            std::numeric_limits<std::uint32_t>::max());
        const auto clamped =
            elapsed < 0 ? 0 : (elapsed > max_value ? max_value : elapsed);

        sink_->fetch_add(static_cast<std::uint32_t>(clamped),
                         std::memory_order_relaxed);
        active_ = false;
    }

  private:
    std::atomic<std::uint32_t> *sink_ = nullptr;
    clock::time_point start_{};
    bool active_ = true;
};

} // namespace axvp::internal
