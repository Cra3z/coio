#pragma once
#include <atomic>
#include <coio/detail/config.h>

namespace coio {
    class atomutex {
    public:
        atomutex() = default;

        atomutex(const atomutex&) = delete;

        ~atomutex() = default;

        auto operator= (const atomutex&) -> atomutex& = delete;

        COIO_ALWAYS_INLINE auto lock() noexcept -> void {
            while (flag_.test_and_set(std::memory_order_acquire)) {
                flag_.wait(true, std::memory_order_relaxed);
            }
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto try_lock() noexcept -> bool {
            return not flag_.test_and_set(std::memory_order_acquire);
        }

        COIO_ALWAYS_INLINE auto unlock() noexcept -> void {
            flag_.clear(std::memory_order_release);
            flag_.notify_one();
        }

    private:
        std::atomic_flag flag_{};
    };
}