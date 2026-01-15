#pragma once
#include <coroutine>
#include <tuple>
#include <utility>
#include <variant>
#include "detail/config.h"
#include "detail/concepts.h"
#include "detail/co_memory.h"
#include "detail/co_promise.h"
#include "detail/execution.h"
#include "detail/intrusive_stack.h"
#include "detail/unhandled_stopped.h"
#include "utils/retain_ptr.h"
#include "utils/utility.h"
#include "utils/stop_token.h"

namespace coio {
    namespace detail {
        template<typename T, typename Promise>
        class task_awaiter {
        public:
            task_awaiter(std::coroutine_handle<Promise> coro, bool owned) noexcept : coro_(coro), owned_(owned) {}

            task_awaiter(const task_awaiter&) = delete;

            ~task_awaiter() {
                if (owned_ and coro_) coro_.destroy();
            }

            auto operator= (const task_awaiter&) -> task_awaiter& = delete;

            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename OtherPromise>
            auto await_suspend(std::coroutine_handle<OtherPromise> this_coro) const noexcept -> std::coroutine_handle<> {
                COIO_ASSERT(coro_ != nullptr);
                coro_.promise().set_continuation(this_coro);
                return coro_;
            }

            auto await_resume() const -> T {
                auto& promise = coro_.promise();
                return static_cast<std::add_rvalue_reference_t<T>>(promise.get_result());
            }

        private:
            std::coroutine_handle<Promise> coro_;
            const bool owned_;
        };

        struct task_final_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<>) const noexcept -> std::coroutine_handle<> {
                if (continuation) return continuation;
                return std::noop_coroutine();
            }

            static auto await_resume() noexcept -> void {}

            std::coroutine_handle<> continuation;
        };

        class task_promise_base {
        protected:
            task_promise_base() = default;

        public:
            template<typename OtherPromise>
            auto set_continuation(std::coroutine_handle<OtherPromise> continuation) noexcept -> void {
                continuation_ = continuation;
                if constexpr (stoppable_promise<OtherPromise>) {
                    stopped_callback_ = &stop_stoppable_coroutine_<OtherPromise>;
                }
                else {
                    stopped_callback_ = &default_unhandled_stopped_;
                }
            }

            [[nodiscard]]
            auto continuation() const noexcept -> std::coroutine_handle<> {
                return continuation_;
            }

            [[nodiscard]]
            auto unhandled_stopped() noexcept -> std::coroutine_handle<> {
                return stopped_callback_(continuation_);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() const noexcept -> task_final_awaiter {
                return {continuation_};
            }

        private:
            std::coroutine_handle<> continuation_;
            unhandled_stopped_fn stopped_callback_ = &default_unhandled_stopped_;
        };


        template<typename TaskType, typename T, typename Alloc>
        struct task_promise : task_promise_base, promise_return_control<T>, promise_alloc_control<Alloc> {
            task_promise() = default;

            auto get_return_object() noexcept -> TaskType {
                return std::coroutine_handle<task_promise>::from_promise(*this);
            }

#ifdef COIO_ENABLE_SENDERS
            template<typename Sender> requires requires {
                execution::as_awaitable(std::declval<Sender>(), std::declval<task_promise&>());
            }
            decltype(auto) await_transform(Sender&& sender) noexcept(
                noexcept(execution::as_awaitable(std::declval<Sender>(), std::declval<task_promise&>()))
            ) {
                return execution::as_awaitable(std::forward<Sender>(sender), *this);
            }
#endif
        };
    }

    template<typename T = void, typename Alloc = void>
    class task {
        static_assert(
            std::same_as<T, void> or
            (unqualified_object<T> and std::move_constructible<T>) or
            std::is_lvalue_reference_v<T>,
            "type `T` shall be `void` or a move-constructible object-type or a lvaue-reference type."
        );

        static_assert(
            std::is_void_v<Alloc> or simple_allocator<Alloc>,
            "type `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
        );

        friend detail::task_promise<task, T, Alloc>;

    public:
        using promise_type = detail::task_promise<task, T, Alloc>;

    private:
        task(std::coroutine_handle<promise_type> coro) noexcept : coro_{coro} {}

    public:
        task() = default;

        task(const task&) = delete;

        task(task&& other) noexcept : coro_{std::exchange(other.coro_, {})} {}

        ~task() {
            if (coro_) coro_.destroy();
        }

        auto operator= (const task&) -> task& = delete;

        auto operator= (task&& other) noexcept -> task& {
            task(std::move(other)).swap(*this);
            return *this;
        }

        explicit operator bool() const noexcept {
            return coro_ != nullptr;
        }

        auto operator co_await() && noexcept -> detail::task_awaiter<T, promise_type> {
            COIO_ASSERT(coro_ != nullptr);
            return {std::exchange(coro_, {}), true};
        }

        auto swap(task& other) noexcept -> void {
            std::swap(coro_, other.coro_);
        }

        friend auto swap(task& lhs, task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
    };


    namespace detail {
        struct shared_task_base {
            shared_task_base() = default;

            shared_task_base(const shared_task_base&) = delete;

            auto operator=(const shared_task_base&) -> shared_task_base& = delete;

            std::coroutine_handle<> continuation;
        };

        template<typename T, typename Promise>
        struct shared_task_awaiter : shared_task_base {
            shared_task_awaiter(std::coroutine_handle<Promise> coro) noexcept : coro(coro) {}

            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename ResumerPromise>
            auto await_suspend(std::coroutine_handle<ResumerPromise> this_coro) noexcept -> std::coroutine_handle<> {
                COIO_ASSERT(continuation == nullptr);
                continuation = this_coro;
                if constexpr (stoppable_promise<ResumerPromise>) {
                    stopped_callback_ = &stop_stoppable_coroutine_<ResumerPromise>;
                }
                return coro.promise().add_listener(*this);
            }

            auto await_resume() -> add_const_lvalue_ref_t<T> {
                return coro.promise().get_result();
            }

            auto cancel() noexcept -> void {
                stopped_callback_(continuation).resume();
            }

            std::coroutine_handle<Promise> coro;
            shared_task_awaiter* next = nullptr;
            unhandled_stopped_fn stopped_callback_ = &default_unhandled_stopped_;
        };

        struct shared_task_final_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename SharedTaskPromise>
            static auto await_suspend(std::coroutine_handle<SharedTaskPromise> this_coro) noexcept -> void {
                auto node = this_coro.promise().continuations_.pop_all();
                while (node) {
                    auto next = node->next;
                    node->continuation.resume();
                    node = next;
                }
            }

            static auto await_resume() noexcept -> void {}
        };

        template<typename TaskType, typename T, typename Alloc>
        struct shared_task_promise : promise_return_control<T>, promise_alloc_control<Alloc> {
            friend shared_task_final_awaiter;
            friend shared_task_awaiter<T, shared_task_promise>;

            using handle_type = std::coroutine_handle<shared_task_promise>;

            shared_task_promise() = default;

            auto get_return_object() noexcept -> TaskType {
                return std::coroutine_handle<shared_task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            static auto final_suspend() noexcept -> shared_task_final_awaiter {
                return {};
            }
#ifdef COIO_ENABLE_SENDERS
            template<typename Sender> requires requires {
                execution::as_awaitable(std::declval<Sender>(), std::declval<shared_task_promise&>());
            }
            decltype(auto) await_transform(Sender&& sender) noexcept(
                noexcept(execution::as_awaitable(std::declval<Sender>(), std::declval<shared_task_promise&>()))
            ) {
                return execution::as_awaitable(std::forward<Sender>(sender), *this);
            }
#endif
            auto retain() noexcept -> void {
                ref_count_.fetch_add(1, std::memory_order_relaxed);
            }

            auto lose() noexcept -> void {
                if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    auto handle = handle_type::from_promise(*this);
                    handle.destroy();
                }
            }

            auto add_listener(shared_task_awaiter<T, shared_task_promise>& awaiter) noexcept -> std::coroutine_handle<> {
                const auto this_coro = std::coroutine_handle<shared_task_promise>::from_promise(*this);
                COIO_ASSERT(awaiter.coro == this_coro);
                switch (continuations_.push(awaiter)) {
                using enum stack_status;
                case empty_and_never_pushed: return this_coro;
                case empty_but_pushed: return awaiter.continuation;
                case not_empty: return std::noop_coroutine();
                default: unreachable();
                }
            }

            auto unhandled_stopped() noexcept -> std::coroutine_handle<> {
                auto node = continuations_.pop_all();
                while (node) {
                    auto next = node->next;
                    node->cancel();
                    node = next;
                }
                return std::noop_coroutine();
            }

        private:
            intrusive_stack<shared_task_awaiter<T, shared_task_promise>> continuations_{&shared_task_awaiter<T, shared_task_promise>::next};
            std::atomic<std::size_t> ref_count_{0};
        };
    }


    template<typename T = void, typename Alloc = void>
    class shared_task {
        static_assert(
            std::same_as<T, void> or
            unqualified_object<T> or
            std::is_lvalue_reference_v<T>,
            "type `T` shall be `void` or an object-type or a lvaue-reference type."
        );
        static_assert(
            simple_allocator<Alloc>,
            "typename `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
        );

        friend detail::shared_task_promise<shared_task, T, Alloc>;

    public:
        using promise_type = detail::shared_task_promise<shared_task, T, Alloc>;

    private:
        shared_task(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro), retain_(coro ? &coro.promise() : nullptr) {} // NOLINT

    public:
        shared_task() noexcept = default;

        shared_task(const shared_task& other) = default;

        shared_task(shared_task&& other) noexcept : coro_(std::exchange(other.coro_, {})), retain_(std::move(other.retain_)) {}

        ~shared_task() = default;

        auto operator= (shared_task other) noexcept -> shared_task& {
            shared_task{std::move(other)}.swap(*this);
            return *this;
        }

        explicit operator bool() const noexcept {
            return coro_ != nullptr;
        }

        auto operator co_await() const noexcept -> detail::shared_task_awaiter<T, promise_type> {
            COIO_ASSERT(coro_ != nullptr);
            return {coro_};
        }

        auto swap(shared_task& other) noexcept -> void {
            std::ranges::swap(coro_, other.coro_);
            std::ranges::swap(retain_, other.retain_);
        }

        friend auto swap(shared_task& lhs, shared_task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
        retain_ptr<promise_type> retain_;
    };
}