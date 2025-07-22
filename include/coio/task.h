#pragma once
#include <atomic>
#include <coroutine>
#include <tuple>
#include <utility>
#include <variant>
#include "config.h"
#include "concepts.h"
#include "error.h"
#include "detail/co_memory.h"
#include "utils/retain_ptr.h"
#include "utils/type_traits.h"

namespace coio {
    struct nothing {};

    template<typename T = void, typename Alloc = void>
    class [[nodiscard]] task;

    template<typename T = void, typename Alloc = void>
    class [[nodiscard]] shared_task;

    namespace detail {
        template<typename>
        struct task_traits;

        template<typename T, typename Alloc>
        struct task_traits<task<T, Alloc>> {
            using elem_t = T;
            using alloc_t = Alloc;
            using promise_t = typename std::coroutine_traits<task<T, Alloc>>::promise_type;
        };

        template<typename T, typename Alloc>
        struct task_traits<shared_task<T, Alloc>> {
            using elem_t = T;
            using alloc_t = Alloc;
            using promise_t = typename std::coroutine_traits<shared_task<T, Alloc>>::promise_type;
        };

        template<typename T>
        using task_elem_t = typename task_traits<T>::elem_t;

        template<typename T>
        using task_alloc_t = typename task_traits<T>::alloc_t;

        template<typename T>
        using task_promise_t = typename task_traits<T>::promise_t;

        template<typename T>
        class promise_return_control {
        public:
            auto return_value(T value) noexcept(std::is_nothrow_constructible_v<wrap_ref_t<T>, T>) -> void {
                result_.template emplace<1>(static_cast<T&&>(value));
            }

            auto unhandled_exception() noexcept -> void {
                result_.template emplace<2>(std::current_exception());
            }

            auto get_result() -> T& {
                COIO_ASSERT(result_.index() > 0);
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
                return *std::get_if<1>(&result_);
            }

        private:
            std::variant<std::monostate, wrap_ref_t<T>, std::exception_ptr> result_;
        };

        template<>
        class promise_return_control<void> {
        public:

            auto return_void() noexcept -> void {
                result_.emplace<1>();
            }

            auto unhandled_exception() noexcept -> void {
                result_.emplace<2>(std::current_exception());
            }

            auto get_result() -> void {
                COIO_ASSERT(result_.index() > 0);
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
            }

        private:
            std::variant<std::monostate, nothing, std::exception_ptr> result_;
        };

        template<typename TaskType>
        struct task_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) const noexcept -> std::coroutine_handle<> {
                COIO_DCHECK(coro != nullptr);
                coro.promise().prev_coro_ = this_coro;
                return coro;
            }

            auto await_resume() const -> task_elem_t<TaskType> {
                auto& promise = coro.promise();
                return static_cast<std::add_rvalue_reference_t<task_elem_t<TaskType>>>(promise.get_result());
            }

            std::coroutine_handle<task_promise_t<TaskType>> coro;
        };

        struct task_final_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<>) const noexcept -> std::coroutine_handle<> {
                if (prev_coro) return prev_coro;
                return std::noop_coroutine();
            }

            static auto await_resume() noexcept -> void {}

            std::coroutine_handle<> prev_coro;
        };

        struct shared_task_node {
            shared_task_node() = default;

            shared_task_node(const shared_task_node&) = delete;

            auto operator=(const shared_task_node&) -> shared_task_node& = delete;

            std::coroutine_handle<> waiter;
            shared_task_node* next = nullptr;
        };

        template<typename SharedTaskType>
        struct shared_task_awaiter : shared_task_node {

            shared_task_awaiter(std::coroutine_handle<task_promise_t<SharedTaskType>> coro) noexcept : coro(coro) {}

            auto await_ready() noexcept -> bool {
                COIO_DCHECK(coro != nullptr);
                return coro.promise().step_ == 2;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> std::coroutine_handle<> {
                waiter = this_coro;
                next = coro.promise().head_.exchange(this, std::memory_order_acq_rel);
                if (int zero = 0; coro.promise().step_.compare_exchange_strong(zero, 1)) {
                    return coro;
                }
                return std::noop_coroutine();
            }

            auto await_resume() -> add_const_lvalue_ref_t<task_elem_t<SharedTaskType>> {
                return coro.promise().get_result();
            }

            std::coroutine_handle<task_promise_t<SharedTaskType>> coro;
        };

        struct shared_task_final_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename SharedTaskPromise>
            static auto await_suspend(std::coroutine_handle<SharedTaskPromise> this_coro) noexcept -> void {
                shared_task_node* awaiter_ = this_coro.promise().head_.load(std::memory_order_acquire);
                while (awaiter_) {
                    auto next = awaiter_->next;
                    awaiter_->waiter.resume();
                    awaiter_ = next;
                }
            }

            static auto await_resume() noexcept -> void {}
        };

        template<typename TaskType>
        struct task_promise : promise_return_control<task_elem_t<TaskType>>, promise_alloc_control<task_alloc_t<TaskType>> {
            friend task_awaiter<TaskType>;

            task_promise() = default;

            auto get_return_object() noexcept -> TaskType {
                return std::coroutine_handle<task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() const noexcept -> task_final_awaiter {
                return {prev_coro_};
            }

        private:
            std::coroutine_handle<> prev_coro_;
        };

        template<typename SharedTaskType>
        struct shared_task_promise : promise_return_control<task_elem_t<SharedTaskType>>, promise_alloc_control<task_alloc_t<SharedTaskType>> {
            friend shared_task_final_awaiter;
            friend shared_task_awaiter<SharedTaskType>;

            using handle_type = std::coroutine_handle<shared_task_promise>;

            shared_task_promise() = default;

            auto get_return_object() noexcept -> SharedTaskType {
                return std::coroutine_handle<shared_task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() noexcept -> shared_task_final_awaiter {
                step_ = 2;
                step_.notify_all();
                return {};
            }

            auto retain() noexcept -> void {
                ref_count_.fetch_add(1, std::memory_order_relaxed);
            }

            auto lose() noexcept -> void {
                if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    auto handle = handle_type::from_promise(*this);
                    handle.destroy();
                }
            }

        private:
            std::atomic<shared_task_node*> head_{nullptr};
            std::atomic<std::size_t> ref_count_{0};
            std::atomic_unsigned_lock_free step_{0}; // 0: not started, 1: started, 2: finished
        };

        struct to_awaiter_fn {
            template<awaitable Awaitable>
            COIO_STATIC_CALL_OP awaiter decltype(auto) operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST noexcept(get_awaiter<Awaitable>::nothrow) {
                if constexpr (requires { operator co_await(std::declval<Awaitable>()); }) {
                    return operator co_await(std::forward<Awaitable>(awt));
                }
                else if constexpr (requires {std::declval<Awaitable>().operator co_await(); }) {
                    return std::forward<Awaitable>(awt).operator co_await();
                }
                else return std::forward<Awaitable>(awt);
            }
        };

    }

    template<typename T, typename Alloc>
    class task {
        static_assert(
            std::same_as<T, void> or
            (unqualified_object<T> and std::move_constructible<T>) or
            std::is_lvalue_reference_v<T>,
            "type `T` shall be `void` or a move-constructible object-type or a lvaue-reference type."
        );

        static_assert(
            detail::valid_coroutine_alloctor_<Alloc>,
            "type `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
        );

        friend detail::task_promise<task>;

    public:
        using promise_type = detail::task_promise<task>;

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

        auto operator co_await() const -> detail::task_awaiter<task> {
            if (coro_ == nullptr) [[unlikely]] throw error::task_error{error::task_errc::no_state};
            if (coro_.done()) [[unlikely]] throw error::task_error{error::task_errc::already_retrieved};
            return {coro_};
        }

        auto swap(task& other) noexcept -> void {
            std::swap(coro_, other.coro_);
        }

        friend auto swap(task& lhs, task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

        [[nodiscard]]
        auto ready() const noexcept -> bool {
            COIO_DCHECK(coro_ != nullptr);
            return coro_.done();
        }

        auto reset() noexcept -> void {
            task{}.swap(*this);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
    };

    template<typename T, typename Alloc>
    class shared_task {
        static_assert(
            std::same_as<T, void> or
            unqualified_object<T> or
            std::is_lvalue_reference_v<T>,
            "type `T` shall be `void` or an object-type or a lvaue-reference type."
        );
        static_assert(
            detail::valid_coroutine_alloctor_<Alloc>,
            "typename `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
        );

        friend detail::shared_task_promise<shared_task>;

    public:
        using promise_type = detail::shared_task_promise<shared_task>;

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

        auto operator co_await() const -> detail::shared_task_awaiter<shared_task> {
            if (coro_ == nullptr) [[unlikely]] throw error::task_error{error::task_errc::no_state};
            return {coro_};
        }

        [[nodiscard]]
        auto ready() const noexcept -> bool {
            COIO_DCHECK(coro_ != nullptr);
            return coro_.promise().step_ == 2;
        }

        auto swap(shared_task& other) noexcept -> void {
            std::ranges::swap(coro_, other.coro_);
            std::ranges::swap(retain_, other.retain_);
        }

        friend auto swap(shared_task& lhs, shared_task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

        auto reset() noexcept -> void {
            shared_task{}.swap(*this);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
        retain_ptr<promise_type> retain_;
    };
}