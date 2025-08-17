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
#include "detail/co_promise.h"
#include "detail/exec.h"
#include "utils/retain_ptr.h"
#include "utils/type_traits.h"

namespace coio {
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

        template<typename TaskType>
        struct task_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> this_coro) const noexcept -> std::coroutine_handle<> {
                COIO_DCHECK(coro != nullptr);
                coro.promise().set_continuation(this_coro);
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
                if (continuation) return continuation;
                return std::noop_coroutine();
            }

            static auto await_resume() noexcept -> void {}

            std::coroutine_handle<> continuation;
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

            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> std::coroutine_handle<> {
                waiter = this_coro;
                std::coroutine_handle<> continuations[] {coro, std::noop_coroutine(), this_coro};
                auto& step = coro.promise().step_;
                auto index = step.load(std::memory_order_relaxed);
                while (!step.compare_exchange_strong(index, index == 0 ? 1 : index, std::memory_order_relaxed)) {}
                if (index < 2) {
                    next = coro.promise().head_.exchange(this, std::memory_order_acq_rel);
                }
                return continuations[index];
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
                shared_task_node* node = this_coro.promise().head_.load(std::memory_order_acquire);
                while (node) {
                    auto next = node->next;
                    node->waiter.resume();
                    node = next;
                }
            }

            static auto await_resume() noexcept -> void {}
        };

        template<typename TaskType>
        struct task_promise :
            enable_await_senders<task_promise<TaskType>>,
            promise_return_control<task_elem_t<TaskType>>,
            promise_alloc_control<task_alloc_t<TaskType>>
        {
            friend task_awaiter<TaskType>;

            task_promise() = default;

            auto get_return_object() noexcept -> TaskType {
                return std::coroutine_handle<task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() const noexcept -> task_final_awaiter {
                auto continuation_ = this->continuation();
#if defined(COIO_ENABLE_SENDERS) and defined(COIO_EXECUTION_USE_NVIDIA) // TODO: https://github.com/NVIDIA/stdexec/issues/1610
                return {continuation_.handle()};
#else
                return {continuation_};
#endif
            }
        };

        template<typename SharedTaskType>
        struct shared_task_promise :
            enable_await_senders<shared_task_promise<SharedTaskType>>,
            promise_return_control<task_elem_t<SharedTaskType>>,
            promise_alloc_control<task_alloc_t<SharedTaskType>>
        {
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
                step_.store(2, std::memory_order_relaxed);
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
            return coro_.promise().step_.load(std::memory_order_relaxed) == 2;
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