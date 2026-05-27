#pragma once
#include <coroutine>
#include <tuple>
#include <utility>
#include <variant>
#include <coio/detail/config.h>
#include <coio/detail/concepts.h>
#include <coio/detail/co_memory.h>
#include <coio/detail/execution.h>
#include <coio/utils/allocator_resource.h>
#include <coio/utils/polymorphic_scheduler.h>
#include <coio/utils/stop_token.h>
#include <coio/utils/utility.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio {
    namespace detail {
        template<typename Sched, typename Env>
        COIO_ALWAYS_INLINE auto make_task_scheduler(const Env& env) -> Sched {
            return std::make_obj_using_allocator<Sched>(
                (get_suitable_allocator)(env),
                (get_suitable_start_scheduler)(env)
            );
        }

        template<typename Sndr>
        COIO_ALWAYS_INLINE auto affined_sndr(Sndr&& sndr) noexcept {
            if constexpr (requires { std::declval<Sndr>().affine(); }) {
                return std::forward<Sndr>(sndr).affine();
            }
            else {
                return execution::affine(std::forward<Sndr>(sndr));
            }
        }

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<typename T>
        class task_state_base {
        private:
            using value_t = std::conditional_t<std::same_as<T, void>, std::monostate, wrap_ref_t<T>>;
            using result_t = std::variant<std::monostate, value_t, std::exception_ptr>;

        public:
            using operation_state_concept = execution::operation_state_tag;

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
            COIO_ALWAYS_INLINE auto dispose_with_value(Args&&... args) -> void {
                result_.template emplace<1>(std::forward<Args>(args)...);
            }

            COIO_ALWAYS_INLINE auto dispose_with_exception(std::exception_ptr exp) noexcept -> void {
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
            task_operation(std::coroutine_handle<Promise> coro, Receiver rcvr) noexcept :
                base(coro),
                rcvr_(std::move(rcvr)),
                stop_propagator_(coio::get_stop_token(execution::get_env(rcvr_))) {
                coro.promise().sched_.emplace((make_task_scheduler<typename Promise::scheduler_type>)(execution::get_env(rcvr_)));
            }

            COIO_ALWAYS_INLINE auto start() & noexcept -> void {
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
                    execution::set_stopped(std::move(rcvr_));
                    break;
                }
                case 1: {
                    if constexpr (std::same_as<T, void>) {
                        execution::set_value(std::move(rcvr_));
                    }
                    else {
                        execution::set_value(std::move(rcvr_), static_cast<T&&>(std::get<1>(this->result_)));
                    }
                    break;
                }
                case 2: {
                    execution::set_error(std::move(rcvr_), std::move(std::get<2>(this->result_)));
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
                stop_propagator_(coio::get_stop_token(execution::get_env(receiver))) {
                coro.promise().sched_.emplace((make_task_scheduler<typename Promise::scheduler_type>)(execution::get_env(receiver)));
            }

            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            COIO_ALWAYS_INLINE auto await_suspend(std::coroutine_handle<RcvrPromise> continuation) noexcept -> std::coroutine_handle<> {
                COIO_ASSERT(continuation_ == continuation);
                static_cast<void>(continuation);
                const auto coro = std::coroutine_handle<Promise>::from_address(this->coro_.address());
                coro.promise().state_ = this;
                return coro;
            }

            COIO_ALWAYS_INLINE auto await_resume() -> T {
                switch (this->result_.index()) {
                case 1: {
                    return static_cast<std::add_rvalue_reference_t<T>>(std::get<1>(this->result_));
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
                    auto& promise = std::coroutine_handle<Promise>::from_address(continuation_.address()).promise();
                    return promise.unhandled_stopped();
                }
                return continuation_;
            }

        private:
            std::coroutine_handle<RcvrPromise> continuation_;
            stop_propagator<inplace_stop_source, stop_token_of_rcvr> stop_propagator_;
        };

        template<typename TaskType, typename T, typename Alloc, typename Sched>
        struct task_promise;

        struct task_final_awaiter {
            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename... Args>
            COIO_ALWAYS_INLINE auto await_suspend(std::coroutine_handle<task_promise<Args...>> this_coro) const noexcept -> std::coroutine_handle<> {
                return this_coro.promise().state_->complete();
            }

            COIO_ALWAYS_INLINE static auto await_resume() noexcept -> void {}
        };


        template<typename T>
        struct task_promise_base {
            task_promise_base() = default;

            COIO_ALWAYS_INLINE static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            COIO_ALWAYS_INLINE static auto final_suspend() noexcept -> task_final_awaiter {
                return {};
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
            COIO_ALWAYS_INLINE auto return_value(auto&& init) -> void {
                this->state_->dispose_with_value(std::forward<decltype(init)>(init));
            }
        };

        template<>
        struct task_promise_return<void> : task_promise_base<void> {
            // ReSharper disable once CppMemberFunctionMayBeConst
            COIO_ALWAYS_INLINE auto return_void() -> void {
                this->state_->dispose_with_value();
            }
        };


        template<typename TaskType, typename T, typename Alloc, typename Sched>
        struct task_promise : task_promise_return<T>, promise_alloc_control<Alloc> {
            using value_type = T;
            using allocator_type = Alloc;
            using scheduler_type = Sched;
            struct env {
                auto query(get_allocator_t) const noexcept {
                    return promise->alloc_adaptor_.get_allocator();
                }

                auto query(get_stop_token_t) const noexcept {
                    return promise->state_->get_stop_token();
                }

                auto query(execution::get_start_scheduler_t) const noexcept {
                    return *promise->sched_;
                }

                auto query(execution::get_scheduler_t) const noexcept {
                    return query(execution::get_start_scheduler);
                }

                const task_promise* promise;
            };

            task_promise() = default;

            task_promise(std::allocator_arg_t, auto alloc, const auto&...) noexcept : alloc_adaptor_(std::move(alloc)) {}

            task_promise(const auto&, std::allocator_arg_t, auto alloc, const auto&...) noexcept : alloc_adaptor_(std::move(alloc)) {}

            COIO_ALWAYS_INLINE auto get_return_object() noexcept -> TaskType {
                return std::coroutine_handle<task_promise>::from_promise(*this);
            }

            template<execution::sender Sndr>
            COIO_ALWAYS_INLINE decltype(auto) await_transform(Sndr&& sndr) noexcept {
                return execution::as_awaitable(detail::affined_sndr(std::forward<Sndr>(sndr)), *this);
            }

            COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                return env{this};
            }

            COIO_NO_UNIQUE_ADDRESS allocator_adaptor<Alloc> alloc_adaptor_;
            std::optional<Sched> sched_;
        };
    }

    template<typename T = void, typename Alloc = void, typename Sched = polymorphic_scheduler>
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

        static_assert(detail::infallible_scheduler<Sched, execution::env<>>);

        friend detail::task_promise<task, T, Alloc, Sched>;

    public:
        using value_type = T;
        using allocator_type = Alloc;
        using scheduler_type = Sched;
        using promise_type = detail::task_promise<task, T, Alloc, Sched>;
        using sender_concept = execution::sender_tag;
        using completion_signatures =execution::completion_signatures<
            detail::set_value_t<T>,
            execution::set_error_t(std::exception_ptr),
            execution::set_stopped_t()
        >;

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

        COIO_ALWAYS_INLINE explicit operator bool() const noexcept {
            return coro_ != nullptr;
        }

        template<similar_to<task>, typename...>
        static consteval auto get_completion_signatures() noexcept -> completion_signatures {
            return {};
        }

        COIO_ALWAYS_INLINE auto affine() && noexcept -> task {
            return std::move(*this);
        }

        template<stoppable_promise ReceiverPromise>
        COIO_ALWAYS_INLINE auto as_awaitable(ReceiverPromise& receiver) && noexcept {
            return detail::task_awaiter<T, promise_type, ReceiverPromise>{
                std::exchange(coro_, {}),
                receiver
            };
        }

        template<execution::receiver Receiver>
        COIO_ALWAYS_INLINE auto connect(Receiver receiver) && noexcept {
            return detail::task_operation<T, promise_type, Receiver>{
                std::exchange(coro_, {}),
                std::move(receiver)
            };
        }

        COIO_ALWAYS_INLINE auto swap(task& other) noexcept -> void {
            std::swap(coro_, other.coro_);
        }

        COIO_ALWAYS_INLINE friend auto swap(task& lhs, task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
    };

}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
