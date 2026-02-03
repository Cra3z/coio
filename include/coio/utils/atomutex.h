#pragma once
#include <atomic>

namespace coio {
    class atomutex {
    public:
        atomutex() = default;

        atomutex(const atomutex&) = delete;

        ~atomutex() = default;

        auto operator= (const atomutex&) -> atomutex& = delete;

        auto lock() noexcept -> void {
            while (flag_.test_and_set(std::memory_order_acquire)) {
                flag_.wait(true, std::memory_order_relaxed);
            }
        }

        [[nodiscard]]
        auto try_lock() noexcept -> bool {
            return not flag_.test_and_set(std::memory_order_acquire);
        }

        auto unlock() noexcept -> void {
            flag_.clear(std::memory_order_release);
            flag_.notify_one();
        }

    private:
        std::atomic_flag flag_{};
    };
}