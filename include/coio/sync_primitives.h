#pragma once
#include <atomic>
#include <bit>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include "core.h"

namespace coio {
    template<typename Mutex>
    concept basic_async_lockable = requires (Mutex&& mtx) {
        { mtx.lock() } -> execution::sender;
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
            COIO_ASSERT(mtx_ != nullptr);
            COIO_ASSERT(owned_);
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
        using acquire_sender = task<>;
        using release_sender = task<>;
    private:
        class acquire_awaiter {
            friend async_semaphore;
        private:
            acquire_awaiter(async_semaphore& sema) noexcept : sema_(sema) {}

        public:
            acquire_awaiter(const acquire_awaiter&) = delete;

            auto operator= (const acquire_awaiter&) -> acquire_awaiter& = delete;

            auto await_ready() const noexcept -> bool {
                if (sema_.try_acquire()) {
                    sema_.mtx_.unlock();
                    return true;
                }
                return false;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
                coro_ = this_coro;
                next_ = std::exchange(sema_.waiting_list_head_, this);
                sema_.mtx_.unlock();
            }

            static auto await_resume() noexcept -> void {}

        private:
            async_semaphore& sema_;
            std::coroutine_handle<> coro_;
            acquire_awaiter* next_ = nullptr;
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
            co_await mtx_.lock();
            co_await acquire_awaiter{*this};
        }

        [[nodiscard]]
        auto try_acquire() noexcept -> bool {
            auto current = counter_.load(std::memory_order_acquire);
            do {
                if (current <= 0) return false;
            }
            while (not counter_.compare_exchange_weak(
                current, current - 1,
                std::memory_order_acq_rel, std::memory_order_acquire
            ));
            return true;
        }

        [[nodiscard]]
        auto release() noexcept -> release_sender {
            auto lock_guard = co_await mtx_.lock_guard();
            if (waiting_list_head_) {
                auto continuation = waiting_list_head_->coro_;
                waiting_list_head_ = waiting_list_head_->next_;
                lock_guard.unlock();
                continuation.resume();
            }
            else {
                lock_guard.unlock();
                auto current = counter_.load(std::memory_order_acquire);
                do {
                    if (current == max()) std::terminate();
                }
                while (not counter_.compare_exchange_weak(
                    current, current + 1,
                    std::memory_order_acq_rel, std::memory_order_acquire
                ));
            }
        }

        [[nodiscard]]
        auto count() const noexcept -> count_type {
            return counter_.load(std::memory_order_acquire);
        }

    private:
        std::atomic_signed_lock_free counter_;
        async_mutex mtx_;
        acquire_awaiter* waiting_list_head_{};
    };

    using async_binary_semaphore = async_semaphore<1>;


    class async_latch {
    public:
        using count_type = std::atomic_unsigned_lock_free::value_type;

    private:
        class wait_sender {
            friend class async_latch;
        private:
            struct awaiter {
                awaiter(async_latch& latch, count_type n) noexcept : latch_{latch}, n_{n} {}

                awaiter(const wait_sender&) = delete;

                auto operator= (const awaiter&) -> awaiter& = delete;

                auto await_ready() noexcept -> bool {
                    return latch_.count_down(n_) == 0;
                }

                auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
                    coro_ = this_coro;
                    latch_.waiting_list_.push(*this);
                }

                static auto await_resume() noexcept -> void {}

                async_latch& latch_; // NOLINT(*-avoid-const-or-ref-data-members)
                count_type n_;
                std::coroutine_handle<> coro_;
                awaiter* next_ = nullptr;
            };

        public:
            wait_sender(async_latch& latch, count_type n) noexcept : latch_(&latch), n_(n) {}

            wait_sender(const wait_sender&) = delete;

            wait_sender(wait_sender&& other) noexcept :
                latch_(std::exchange(other.latch_, {})),
                n_(std::exchange(other.n_, 0)) {}

            auto operator= (const wait_sender&) -> wait_sender& = delete;

            auto operator= (wait_sender&& other) noexcept -> wait_sender& {
                latch_ = std::exchange(other.latch_, {});
                n_ = std::exchange(other.n_, 0);
                return *this;
            }

            auto operator co_await() && noexcept -> awaiter {
                COIO_ASSERT(latch_ != nullptr);
                return {*std::exchange(latch_, {}), std::exchange(n_, 0)};
            }

        private:
            async_latch* latch_ = nullptr;
            count_type n_;
        };

    public:
        explicit async_latch(count_type count) noexcept : counter_(count) {}

        async_latch(const async_latch&) = delete;

        auto operator= (const async_latch&) -> async_latch& = delete;

        [[nodiscard]]
        static constexpr auto max() noexcept -> count_type {
            return std::numeric_limits<count_type>::max();
        }

        [[nodiscard]]
        auto count() const noexcept -> count_type {
            return counter_.load(std::memory_order_acquire);
        }

        [[nodiscard]]
        auto try_wait() const noexcept -> bool {
            return count() == 0;
        }

        auto count_down(count_type n = 1) noexcept -> count_type {
            auto old = counter_.fetch_sub(n, std::memory_order_acq_rel);
            COIO_ASSERT(old >= n);
            if (old == n) {
                auto node = waiting_list_.pop_all();
                while (node) {
                    auto next = node->next_;
                    node->coro_.resume();
                    node = next;
                }
            }
            return old - n;
        }

        [[nodiscard]]
        auto wait() noexcept -> wait_sender {
            return {*this, 0};
        }

        [[nodiscard]]
        auto arrive_and_wait(count_type n = 1) noexcept -> wait_sender {
            return {*this, n};
        }

    private:
        std::atomic_unsigned_lock_free counter_;
        detail::intrusive_stack<wait_sender::awaiter> waiting_list_{&wait_sender::awaiter::next_};
    };
}
