#pragma once
#include <atomic>
#include <coroutine>
#include <tuple>
#include <utility>
#include <variant>
#include "detail/config.h"
#include "concepts.h"
#include "detail/co_memory.h"
#include "detail/co_promise.h"
#include "detail/exec.h"
#include "detail/intrusive_stack.h"
#include "utils/retain_ptr.h"
#include "utils/type_traits.h"
#include "utils/utility.h"

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

        [[noreturn]]
        inline auto default_unhandled_stopped_(std::coroutine_handle<>) noexcept -> std::coroutine_handle<> {
            std::terminate();
        }

        template<stoppable_promise Promise>
        [[nodiscard]]
        auto stop_stoppable_coroutine_(std::coroutine_handle<> coro) noexcept -> std::coroutine_handle<> {
            auto& promise = std::coroutine_handle<Promise>::from_address(coro.address()).promise();
            return promise.unhandled_stopped();
        }

        using unhandled_stopped_fn = std::coroutine_handle<>(*)(std::coroutine_handle<>) noexcept;

        template<typename TaskType>
        class task_awaiter {
        private:
            using promise_t = task_promise_t<TaskType>;

        public:
            task_awaiter(std::coroutine_handle<promise_t> coro, bool owned) noexcept : coro_(coro), owned_(owned) {}

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

            auto await_resume() const -> task_elem_t<TaskType> {
                auto& promise = coro_.promise();
                return static_cast<std::add_rvalue_reference_t<task_elem_t<TaskType>>>(promise.get_result());
            }

        private:
            std::coroutine_handle<promise_t> coro_;
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

        struct shared_task_base {
            shared_task_base() = default;

            shared_task_base(const shared_task_base&) = delete;

            auto operator=(const shared_task_base&) -> shared_task_base& = delete;

            std::coroutine_handle<> continuation;
        };

        template<typename SharedTaskType>
        struct shared_task_awaiter : shared_task_base {
            shared_task_awaiter(std::coroutine_handle<task_promise_t<SharedTaskType>> coro) noexcept : coro(coro) {}

            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> std::coroutine_handle<> {
                COIO_ASSERT(continuation == nullptr);
                continuation = this_coro;
                if constexpr (stoppable_promise<Promise>) {
                    stopped_callback_ = &stop_stoppable_coroutine_<Promise>;
                }
                return coro.promise().add_listener(*this);
            }

            auto await_resume() -> add_const_lvalue_ref_t<task_elem_t<SharedTaskType>> {
                return coro.promise().get_result();
            }

            auto cancel() noexcept -> void {
                stopped_callback_(continuation).resume();
            }

            std::coroutine_handle<task_promise_t<SharedTaskType>> coro;
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

        private:
            std::coroutine_handle<> continuation_;
            unhandled_stopped_fn stopped_callback_ = &default_unhandled_stopped_;
        };
        template<typename TaskType>
        struct task_promise :
            task_promise_base,
            promise_return_control<task_elem_t<TaskType>>,
            promise_alloc_control<task_alloc_t<TaskType>>
        {
            task_promise() = default;

            auto get_return_object() noexcept -> TaskType {
                return std::coroutine_handle<task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() const noexcept -> task_final_awaiter {
                return {this->continuation()};
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

        template<typename SharedTaskType>
        struct shared_task_promise :
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

            auto add_listener(shared_task_awaiter<SharedTaskType>& awaiter) noexcept -> std::coroutine_handle<> {
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
            intrusive_stack<shared_task_awaiter<SharedTaskType>> continuations_{&shared_task_awaiter<SharedTaskType>::next};
            std::atomic<std::size_t> ref_count_{0};
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

        auto operator co_await() && noexcept -> detail::task_awaiter<task> {
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

        auto operator co_await() const noexcept -> detail::shared_task_awaiter<shared_task> {
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