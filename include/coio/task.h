#pragma once
#include <coroutine>
#include <tuple>
#include <utility>
#include <variant>
#include "detail/config.h"
#include "detail/concepts.h"
#include "detail/co_memory.h"
#include "detail/execution.h"
#include "detail/unhandled_stopped.h"
#include "utils/stop_token.h"
#include "utils/utility.h"

namespace coio {
    namespace detail {
        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<typename T>
        class task_state_base {
        private:
            using value_t = std::conditional_t<std::same_as<T, void>, std::monostate, wrap_ref_t<T>>;
            using result_t = std::variant<std::monostate, value_t, std::exception_ptr>;

        public:
            using operation_state_concept = execution::operation_state_t;

        public:
            task_state_base(std::coroutine_handle<> coro) noexcept : coro_(coro) {
                COIO_ASSERT(coro_ != nullptr);
            }

            task_state_base(const task_state_base&) = delete;

            ~task_state_base() {
                if (coro_) coro_.destroy();
            }

            auto operator= (const task_state_base&) -> task_state_base& = delete;

            template<typename... Args>
            auto dispose_with_value(Args&&... args) -> void {
                result_.template emplace<1>(std::forward<Args>(args)...);
            }

            auto dispose_with_exception(std::exception_ptr exp) noexcept -> void {
                result_.template emplace<2>(std::move(exp));
            }

            virtual auto get_stop_token() const noexcept -> inplace_stop_token = 0;

            virtual auto complete() noexcept -> std::coroutine_handle<> = 0;

        protected:
            std::coroutine_handle<> coro_;
            result_t result_;
        };


        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<typename T, typename Promise, typename Receiver>
        class task_operation : public task_state_base<T> {
        private:
            using base = task_state_base<T>;
            using stop_token_of_rcvr = stop_token_of_t<execution::env_of_t<Receiver>>;

        public:
            task_operation(std::coroutine_handle<Promise> coro, Receiver rcvr) :
                base(coro),
                rcvr_(std::move(rcvr)),
                stop_propagator_(coio::get_stop_token(execution::get_env(rcvr_))) {}

            auto start() & noexcept -> void {
                const auto coro = std::coroutine_handle<Promise>::from_address(this->coro_.address());
                coro.promise().state_ = this;
                coro.resume();
            }

            auto get_stop_token() const noexcept -> inplace_stop_token override {
                return stop_propagator_.get_token();
            }

            auto complete() noexcept -> std::coroutine_handle<> override {
                switch (this->result_.index()) {
                case 0: {
                    execution::set_stopped(rcvr_);
                    break;
                }
                case 1: {
                    if constexpr (std::same_as<T, void>) {
                        execution::set_value(rcvr_);
                    }
                    else {
                        try {
                            execution::set_value(rcvr_, static_cast<T>(std::get<1>(this->result_)));
                        }
                        catch (...) {
                            execution::set_error(rcvr_, std::current_exception());
                        }
                    }
                    break;
                }
                case 2: {
                    execution::set_error(rcvr_, std::move(std::get<2>(this->result_)));
                    break;
                }
                default: unreachable();
                }
                return std::noop_coroutine();
            }

        private:
            Receiver rcvr_;
            stop_propagator<inplace_stop_source, stop_token_of_rcvr> stop_propagator_;
        };


        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<typename T, typename Promise, typename RcvrPromise>
        class task_awaiter : public task_state_base<T> {
        private:
            using base = task_state_base<T>;
            using stop_token_of_rcvr = stop_token_of_t<execution::env_of_t<RcvrPromise>>;
        public:
            task_awaiter(std::coroutine_handle<Promise> coro, RcvrPromise& receiver) noexcept :
                base(coro),
                continuation_(std::coroutine_handle<RcvrPromise>::from_promise(receiver)),
                stop_propagator_(coio::get_stop_token(execution::get_env(receiver)))
            {}

            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            COIO_ALWAYS_INLINE auto await_suspend(std::coroutine_handle<RcvrPromise> continuation) noexcept -> std::coroutine_handle<> {
                COIO_ASSERT(continuation_ == continuation);
                const auto coro = std::coroutine_handle<Promise>::from_address(this->coro_.address());
                coro.promise().state_ = this;
                return coro;
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto await_resume() -> T {
                switch (this->result_.index()) {
                case 1: {
                    return static_cast<T>(std::get<1>(this->result_));
                }
                case 2: {
                    std::rethrow_exception(std::move(std::get<2>(this->result_)));
                }
                default: unreachable();
                }
            }

            auto get_stop_token() const noexcept -> inplace_stop_token override {
                return stop_propagator_.get_token();
            }

            auto complete() noexcept -> std::coroutine_handle<> override {
                if (this->result_.index() == 0) {
                    return (stop_coroutine<RcvrPromise>)(continuation_);
                }
                return continuation_;
            }

        private:
            std::coroutine_handle<RcvrPromise> continuation_;
            stop_propagator<inplace_stop_source, stop_token_of_rcvr> stop_propagator_;
        };


        struct task_final_awaiter {
            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            COIO_ALWAYS_INLINE auto await_suspend(std::coroutine_handle<>) const noexcept -> std::coroutine_handle<> {
                if (continuation) return continuation;
                return std::noop_coroutine();
            }

            COIO_ALWAYS_INLINE static auto await_resume() noexcept -> void {}

            std::coroutine_handle<> continuation;
        };


        template<typename T>
        struct task_promise_base {
            task_promise_base() = default;

            COIO_ALWAYS_INLINE static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            COIO_ALWAYS_INLINE auto final_suspend() const noexcept -> task_final_awaiter {
                return {state_->complete()};
            }

            COIO_ALWAYS_INLINE auto unhandled_exception() noexcept -> void {
                state_->dispose_with_exception(std::current_exception());
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto unhandled_stopped() const noexcept -> std::coroutine_handle<> {
                return state_->complete();
            }

            task_state_base<T>* state_ = nullptr;
        };


        template<typename T>
        struct task_promise_return : task_promise_base<T> {
            using completion_signatures = execution::completion_signatures<
                execution::set_value_t(T),
                execution::set_error_t(std::exception_ptr),
                execution::set_stopped_t()
            >;

            COIO_ALWAYS_INLINE auto return_value(auto&& init) -> void {
                this->state_->dispose_with_value(std::forward<decltype(init)>(init));
            }
        };

        template<>
        struct task_promise_return<void> : task_promise_base<void> {
            using completion_signatures = execution::completion_signatures<
                execution::set_value_t(),
                execution::set_error_t(std::exception_ptr),
                execution::set_stopped_t()
            >;

            COIO_ALWAYS_INLINE auto return_void() -> void {
                this->state_->dispose_with_value();
            }
        };


        template<typename TaskType, typename T, typename Alloc>
        struct task_promise : task_promise_return<T>, promise_alloc_control<Alloc> {
            task_promise() = default;

            COIO_ALWAYS_INLINE auto get_return_object() noexcept -> TaskType {
                return std::coroutine_handle<task_promise>::from_promise(*this);
            }

            template<typename Sender> requires requires {
                execution::as_awaitable(std::declval<Sender>(), std::declval<task_promise&>());
            }
            COIO_ALWAYS_INLINE decltype(auto) await_transform(Sender&& sender) noexcept(
                noexcept(execution::as_awaitable(std::declval<Sender>(), std::declval<task_promise&>()))
            ) {
                return execution::as_awaitable(std::forward<Sender>(sender), *this);
            }

            COIO_ALWAYS_INLINE auto get_env() const noexcept {
                return execution::env{
                    execution::prop{get_stop_token, this->state_->get_stop_token()}
                };
            }
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
        using sender_concept = execution::sender_t;
        using completion_signatures = promise_type::completion_signatures;

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

        template<stoppable_promise ReceiverPromise>
        COIO_ALWAYS_INLINE auto as_awaitable(ReceiverPromise& receiver) && {
            return detail::task_awaiter<T, promise_type, ReceiverPromise>{
                std::exchange(coro_, {}),
                receiver
            };
        }

        template<execution::receiver_of<completion_signatures> Receiver>
        COIO_ALWAYS_INLINE auto connect(Receiver receiver) && {
            return detail::task_operation<T, promise_type, Receiver>{
                std::exchange(coro_, {}),
                std::move(receiver)
            };
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
}