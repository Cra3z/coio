#pragma once
#include <atomic>
#include <bit>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include "config.h"
#include "detail/waiting_list.h"

namespace coio {
    template<typename Mutex>
    concept basic_async_lockable = requires (Mutex&& mtx) {
        { mtx.lock() } -> awaitable_value;
        { mtx.unlock() } -> std::same_as<void>;
        requires std::same_as<detail::await_result_t<decltype(mtx.lock())>, void>;
    };

    template<typename Mutex>
    concept async_lockable = basic_async_lockable<Mutex> and requires (Mutex&& mtx) {
        { mtx.try_lock() } -> boolean_testable;
    };

    template<typename AsyncMutex>
    class async_unique_lock {
        static_assert(basic_async_lockable<AsyncMutex>, "type `AsyncMutex` shall model `coio::basic_async_lockable`");
    public:
        using mutex_type = AsyncMutex;

    public:
        async_unique_lock() = default;

        async_unique_lock(mutex_type& mtx, std::adopt_lock_t) noexcept : mtx_(&mtx), owned_(true) {}

        async_unique_lock(mutex_type& mtx, std::defer_lock_t) noexcept : mtx_(&mtx), owned_(false) {}

        async_unique_lock(mutex_type& mtx, std::try_to_lock_t) requires async_lockable<AsyncMutex> : mtx_(&mtx), owned_(mtx.try_lock()) {}

        async_unique_lock(const async_unique_lock&) = delete;

        async_unique_lock(async_unique_lock&& other) noexcept : mtx_(std::exchange(other.mtx_, {})), owned_(std::exchange(other.owned_, {})) {};

        ~async_unique_lock() {
            if (owned_) [[likely]] {
                COIO_ASSERT(mtx_ != nullptr);
                mtx_->unlock();
            }
        }

        auto operator= (async_unique_lock other) noexcept -> async_unique_lock& {
            this->swap(other);
            return *this;
        }

        auto swap(async_unique_lock& other) noexcept -> void {
            std::swap(mtx_, other.mtx_);
            std::swap(owned_, other.owned_);
        }

        friend auto swap(async_unique_lock& lhs, async_unique_lock& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

        auto lock() {
            validate_();
            return then(mtx_->lock(), [this]() noexcept {
                owned_ = true;
            });
        }

        [[nodiscard]]
        auto try_lock() -> bool requires async_lockable<AsyncMutex> {
            validate_();
            return owned_ = bool(mtx_->try_lock());
        }

        auto unlock() -> void {
            validate_();
            mtx_->unlock();
            owned_ = false;
        }

        [[nodiscard]]
        auto mutex() noexcept -> mutex_type* {
            return mtx_;
        }

        [[nodiscard]]
        auto owns_lock() const noexcept -> bool {
            return owned_;
        }

        explicit operator bool() const noexcept {
            return owns_lock();
        }

        [[nodiscard]]
        auto release() noexcept -> mutex_type* {
            owned_ = false;
            return std::exchange(mtx_, nullptr);
        }

    private:
        auto validate_() const noexcept {
            COIO_ASSERT(mtx_ != nullptr);
            COIO_ASSERT(not owned_);
        }

    private:
        mutex_type* mtx_ = nullptr;
        bool owned_ = false;
    };

    class async_mutex {
    public:
        class lock_guard_sender;

        class lock_sender {
            friend async_mutex;
            friend lock_guard_sender;
        private:
            class awaiter {
                friend async_mutex;
            public:
                awaiter(async_mutex& mtx) noexcept : mtx_(mtx) {}

                awaiter(const awaiter&) = delete;

                auto operator= (const awaiter&) -> awaiter& = delete;

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
                        next_ = std::bit_cast<awaiter*>(old_state);
                        // check whether `old_state` is out of date, if not out of date, let state be `this` and then go back to caller.
                        if (mtx_.state_.compare_exchange_weak(old_state, std::bit_cast<std::uintptr_t>(this))) return true;
                    }
                }

                static auto await_resume() noexcept -> void {}

            protected:
                async_mutex& mtx_;
                std::coroutine_handle<> coro_{};
                awaiter* next_{};
            };

        private:
            lock_sender(async_mutex& mtx) noexcept : mtx_(&mtx) {}

        public:
            lock_sender(const lock_sender&) = delete;

            lock_sender(lock_sender&& other) noexcept : mtx_(std::exchange(other.mtx_, {})) {}

            auto operator= (const lock_sender&) -> lock_sender& = delete;

            auto operator= (lock_sender&& other) noexcept -> lock_sender& {
                mtx_ = std::exchange(other.mtx_, {});
                return *this;
            }

            auto operator co_await() && noexcept -> awaiter {
                COIO_ASSUME(mtx_ != nullptr);
                return awaiter{*std::exchange(mtx_, {})};
            }

        private:
            async_mutex* mtx_;
        };

    public:
        async_mutex() = default;

        async_mutex(const async_mutex&) = delete;

        auto operator= (const async_mutex&) -> async_mutex& = delete;

        [[nodiscard]]
        auto lock() noexcept -> lock_sender {
            return lock_sender{*this};
        }

        [[nodiscard]]
        auto lock_guard() noexcept {
            return then(lock(), [this]() noexcept {
                return async_unique_lock{*this, std::adopt_lock};
            });
        };

        [[nodiscard]]
        auto try_lock() noexcept -> bool {
            auto expected = not_locked;
            return state_.compare_exchange_strong(expected, locked_but_no_waiter);
        }

        auto unlock() -> void {
            lock_sender::awaiter* old_head = head_.load();
            if (old_head == nullptr) {
                auto old_state = locked_but_no_waiter;
                if (state_.compare_exchange_strong(old_state, not_locked)) return;

                old_state = state_.exchange(locked_but_no_waiter);
                // to resume the first waiter at first, we reverse the waiting stack and prepend to waiting list.
                auto current = std::bit_cast<lock_sender::awaiter*>(old_state);
                while (current) {
                    old_head = std::exchange(current, std::exchange(current->next_, old_head));
                }
            }
            head_.store(old_head->next_);
            old_head->coro_.resume();
        }

    private:
        // no lock_sender::awaiter object whose address is 0x01, so we regard 0x01 as a state representing the mutex not locked.
        static constexpr std::uintptr_t not_locked = 1;
        static constexpr std::uintptr_t locked_but_no_waiter = 0;
        std::atomic<std::uintptr_t> state_ = not_locked; // represent no locked or the top of waiting stack
        std::atomic<lock_sender::awaiter*> head_{nullptr}; // waiting list head
    };


    template<std::atomic_signed_lock_free::value_type LeastMaxValue = std::numeric_limits<std::atomic_signed_lock_free::value_type>::max()>
    class async_semaphore {
    public:
        using count_type = std::atomic_signed_lock_free::value_type;

        class acquire_sender {
            friend async_semaphore;
        private:
            class awaiter {
                friend detail::waiting_list<awaiter>;
                friend async_semaphore;
                friend acquire_sender;
            private:
                awaiter(async_semaphore& sema) noexcept : sema_(sema) {}

            public:
                awaiter(const awaiter&) = delete;

                auto operator= (const awaiter&) -> awaiter& = delete;

                auto await_ready() const noexcept -> bool {
                    return sema_.try_acquire();
                }

                auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> bool {
                    coro_ = this_coro;
                    sema_.waiting_list_.push(*this);
                    return true;
                }

                static auto await_resume() noexcept -> void {}

            private:
                async_semaphore& sema_;
                std::coroutine_handle<> coro_;
                awaiter* next_ = nullptr;
            };

            using waiting_list = detail::waiting_list<awaiter>;

        private:
            acquire_sender(async_semaphore& sema) noexcept : sema_{&sema} {}

        public:
            acquire_sender(const acquire_sender&) = delete;

            acquire_sender(acquire_sender&& other) noexcept : sema_(std::exchange(other.sema_, {})) {}

            auto operator= (const acquire_sender&) -> acquire_sender& = delete;

            auto operator= (acquire_sender&& other) noexcept -> acquire_sender& {
                sema_ = std::exchange(other.sema_, {});
                return *this;
            }

            auto operator co_await() && noexcept {
                COIO_ASSUME(sema_ != nullptr);
                return awaiter{*std::exchange(sema_, {})};
            }

        private:
            async_semaphore* sema_;
        };

    public:
        explicit async_semaphore(count_type init) noexcept : counter_(init) {
            COIO_ASSERT(init >= 0 and init <= max());
        }

        async_semaphore(const async_semaphore&) = delete;

        auto operator= (const async_semaphore&) -> async_semaphore& = delete;

        [[nodiscard]]
        static constexpr auto max() noexcept -> count_type {
            return LeastMaxValue;
        }

        [[nodiscard]]
        auto acquire() noexcept -> acquire_sender {
            return acquire_sender{*this};
        }

        [[nodiscard]]
        auto try_acquire() noexcept -> bool {
            auto current = counter_.load(std::memory_order::acquire);
            do {
                if (current <= 0) return false;
            }
            while (not counter_.compare_exchange_weak(
                current, current - 1,
                std::memory_order::acq_rel, std::memory_order::acquire
            ));
            return true;
        }

        auto release() noexcept -> void {
            if (auto front = waiting_list_.pop()) {
                front->coro_.resume();
            }
            else {
                auto current = counter_.load(std::memory_order::acquire);
                do {
                    COIO_ASSERT(current >= 0 and current < max());
                }
                while (not counter_.compare_exchange_weak(
                    current, current + 1,
                    std::memory_order::acq_rel, std::memory_order::acquire
                ));
            }
        }

        [[nodiscard]]
        auto count() const noexcept -> count_type {
            return counter_.load(std::memory_order_acquire);
        }

    private:
        std::atomic_signed_lock_free counter_;
        typename acquire_sender::waiting_list waiting_list_{&acquire_sender::awaiter::next_};
    };

    using async_binary_semaphore = async_semaphore<1>;
}
