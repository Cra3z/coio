#pragma once
#include <atomic>
#include <bit>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include "config.h"

namespace coio {
    class async_mutex;

    class async_lock_guard {
    public:
        explicit async_lock_guard(async_mutex& mtx) noexcept : mtx_(mtx) {}

        async_lock_guard(const async_lock_guard&) = delete;

        auto operator= (const async_lock_guard&) -> async_lock_guard& = delete;

        ~async_lock_guard();
    private:
        async_mutex& mtx_;
    };

    class async_mutex {
    public:
        class make_lock_guard_operation;

        class lock_operation {
            friend async_mutex;
            friend make_lock_guard_operation;
        private:
            lock_operation(async_mutex& mtx) noexcept : mtx_(mtx) {}

        public:
            lock_operation(const lock_operation&) = delete;

            auto operator= (const lock_operation&) -> lock_operation& = delete;

            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> bool {
                coro_ = this_coro;
                while (true) {
                    std::uintptr_t old_state = not_locked;
                    // we guess that the current state is `not_locked`. if we are right, set state `lock_but_no_waiter` and then resume.
                    if (mtx_.state_.compare_exchange_strong(old_state, locked_but_no_waiter)) return false;
                    // we are wrong, it's not `not_locked`, instead of the address of some one lock_operation or null.
                    next_ = std::bit_cast<lock_operation*>(old_state);
                    // check whether `old_state` is out of date, if not out of date, let state be `this` and then go back to caller.
                    if (mtx_.state_.compare_exchange_weak(old_state, std::bit_cast<std::uintptr_t>(this))) return true;
                }
            }

            static auto await_resume() noexcept -> void{}

        private:
            async_mutex& mtx_;
            std::coroutine_handle<> coro_{};
            lock_operation* next_{};
        };

        class make_lock_guard_operation : public lock_operation {
        public:
            using lock_operation::lock_operation;

            [[nodiscard]]
            auto await_resume() noexcept -> async_lock_guard {
                return async_lock_guard{mtx_};
            }
        };

    public:
        async_mutex() = default;

        async_mutex(const async_mutex&) = delete;

        auto operator= (const async_mutex&) -> async_mutex& = delete;

        [[nodiscard]]
        auto lock() noexcept -> lock_operation {
            return lock_operation{*this};
        }

        [[nodiscard]]
        auto make_lock_guard() noexcept -> make_lock_guard_operation {
            return make_lock_guard_operation{*this};
        };

        [[nodiscard]]
        auto try_lock() noexcept -> bool {
            auto expected = not_locked;
            return state_.compare_exchange_strong(expected, locked_but_no_waiter);
        }

        auto unlock() -> void {
            lock_operation* old_head = head_.load();
            if (old_head == nullptr) {
                auto old_state = locked_but_no_waiter;
                if (state_.compare_exchange_strong(old_state, not_locked)) return;

                old_state = state_.exchange(locked_but_no_waiter);
                // to resume the first waiter at first, we reverse the waiting stack and prepend to waiting list.
                auto current = std::bit_cast<lock_operation*>(old_state);
                while (current) {
                    old_head = std::exchange(current, std::exchange(current->next_, old_head));
                }
            }
            head_.store(old_head->next_);
            old_head->coro_.resume();
        }

    private:
        // no lock_operation object whose address is 0x01, so we regard 0x01 as a state representing the mutex not locked.
        static constexpr std::uintptr_t not_locked = 1;
        static constexpr std::uintptr_t locked_but_no_waiter = 0;
        std::atomic<std::uintptr_t> state_ = not_locked; // represent no locked or the top of waiting stack
        std::atomic<lock_operation*> head_{nullptr}; // waiting list head
    };

    inline async_lock_guard::~async_lock_guard() {
        mtx_.unlock();
    }

    template<std::atomic_signed_lock_free::value_type LeastMaxValue = std::numeric_limits<std::atomic_signed_lock_free::value_type>::max()>
    class async_semaphore {
    public:
        using count_type = std::atomic_signed_lock_free::value_type;

        class acquire_operation {
            friend async_semaphore;
        private:
            acquire_operation(async_semaphore& sema) noexcept : sema_{sema} {}

        public:
            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> bool;

            static auto await_resume() noexcept -> void {}
        private:
            async_semaphore& sema_;
        };

    public:
        explicit async_semaphore(count_type n) noexcept : counter_(n) {
            COIO_ASSERT(n >= 0 and n <= max());
        }

        async_semaphore(const async_semaphore&) = delete;

        auto operator= (const async_semaphore&) -> async_semaphore& = delete;

        [[nodiscard]]
        static constexpr auto max() noexcept -> count_type {
            return LeastMaxValue;
        }

        [[nodiscard]]
        auto acquire() noexcept -> acquire_operation {
            return acquire_operation{*this};
        }

        [[nodiscard]]
        auto try_acquire() noexcept -> bool {

        }

        auto release() noexcept -> void {

        }

    private:
        std::atomic_signed_lock_free counter_;
    };

    using async_binary_semaphore = async_semaphore<1>;
}
