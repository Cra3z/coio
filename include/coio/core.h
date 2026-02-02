// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include "task.h"
#include "execution_context.h"
#include "detail/execution.h"
#include "detail/intrusive_stack.h"
#include "detail/manual_lifetime.h"
#include "detail/unhandled_stopped.h"
#include "utils/retain_ptr.h"

namespace coio {
    using execution::scheduler;

    template<typename Scheduler>
    concept timed_scheduler = scheduler<Scheduler> and requires (Scheduler&& sch) {
        { static_cast<Scheduler&&>(sch).now() } -> specialization_of<std::chrono::time_point>;
        { static_cast<Scheduler&&>(sch).schedule_after(static_cast<Scheduler&&>(sch).now().time_since_epoch()) } -> execution::sender;
        { static_cast<Scheduler&&>(sch).schedule_at(static_cast<Scheduler&&>(sch).now()) } -> execution::sender;
    };

    struct get_io_service_t : forwarding_query_t {
        template<typename Env> requires requires(Env env) { env.query(std::declval<get_io_service_t>()); }
        COIO_ALWAYS_INLINE COIO_STATIC_CALL_OP decltype(auto) operator()(const Env& env) COIO_STATIC_CALL_OP_CONST noexcept {
            return env.query(get_io_service_t{});
        }
    };

    inline constexpr get_io_service_t get_io_service{};

    template<typename Scheduler>
    concept io_scheduler = scheduler<Scheduler> and
        std::derived_from<typename std::remove_cvref_t<Scheduler>::scheduler_concept, detail::io_scheduler_t>;

    namespace detail {
        template<typename, typename...>
        struct completion_signature_helper;

        template<typename... PrevSigs, typename... Sigs, typename... Rest>
        struct completion_signature_helper<type_list<PrevSigs...>, execution::completion_signatures<Sigs...>, Rest...> :
            completion_signature_helper<typename type_list<PrevSigs..., Sigs...>::unique, Rest...> {};

        template<typename... Sigs>
        struct completion_signature_helper<type_list<Sigs...>> {
            using types = type_list<Sigs...>;
            using set_value_types = types::template filter<is_set_value>;
            using set_error_types = types::template filter<is_set_error>;
            using set_stopped_types = types::template filter<is_set_stopped>;
            using merged_signatures = execution::completion_signatures<Sigs...>;
        };

        template<typename... CompletionSigs>
        using merge_completion_signatures_t = typename completion_signature_helper<type_list<>, CompletionSigs...>::merged_signatures;

        struct fire_and_forget {
            class promise_type : public promise_alloc_control<void> {
            private:
                template<typename Awaiter>
                struct awaiter_wrapper {
                    COIO_ALWAYS_INLINE decltype(auto) await_ready() noexcept {
                        return inner_.await_ready();
                    }

                    template<typename Promise>
                    COIO_ALWAYS_INLINE decltype(auto) await_suspend(std::coroutine_handle<Promise> this_coro) noexcept {
                        return inner_.await_suspend(this_coro);
                    }

                    COIO_ALWAYS_INLINE auto await_resume() noexcept -> void {
                        if (stopped_) return;
                        void(inner_.await_resume());
                    }

                    Awaiter inner_;
                    bool& stopped_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
                };

            public:
                promise_type() = default;

                template<execution::sender Sndr>
                auto await_transform(Sndr&& sndr) noexcept {
                    if constexpr (awaiter<std::invoke_result_t<execution::as_awaitable_t, Sndr, promise_type&>, promise_type>) {
                        return awaiter_wrapper{
                            execution::as_awaitable(std::forward<Sndr>(sndr), *this),
                            stopped_ = false
                        };
                    }
                    else {
                        return awaiter_wrapper{
                            detail::get_awaiter_impl<promise_type>(execution::as_awaitable(std::forward<Sndr>(sndr), *this)),
                            stopped_ = false
                        };
                    }
                }

                static auto get_return_object() noexcept -> fire_and_forget {
                    return {};
                }

                static auto initial_suspend() noexcept -> std::suspend_never {
                    return {};
                }

                static auto final_suspend() noexcept -> std::suspend_never {
                    return {};
                }

                static auto return_void() noexcept -> void {}

                [[noreturn]]
                static auto unhandled_exception() noexcept -> void {
                    std::terminate();
                }

                auto unhandled_stopped() noexcept -> std::coroutine_handle<> {
                    stopped_ = true;
                    return std::coroutine_handle<promise_type>::from_promise(*this);
                }

            private:
                bool stopped_ = false;
            };
        };
    }


    struct when_any_t {
        template<typename>
        struct env;

        template<typename>
        struct state_base;

        template<execution::receiver, typename, typename>
        struct state_value;

        template<execution::receiver, typename, typename>
        struct receiver;

        template<typename, execution::receiver, typename, typename, execution::sender...>
        struct state;

        template<std::size_t... I, execution::receiver Receiver, typename Value, typename Error, execution::sender... Sender>
        struct state<std::index_sequence<I...>, Receiver, Value, Error, Sender...>;

        template<execution::sender...>
        struct sender;
        
        template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
        COIO_ALWAYS_INLINE COIO_STATIC_CALL_OP auto operator()(Sender&&... sndr) COIO_STATIC_CALL_OP_CONST -> sender<Sender...> {
            return {{std::forward<Sender>(sndr)...}};
        }
    };

    template<typename Receiver>
    struct when_any_t::env {
        COIO_ALWAYS_INLINE auto query(get_stop_token_t) const noexcept -> inplace_stop_token {
            return this->state->source.get_token();
        }

        template<typename Prop, typename... Args>
            requires std::default_initializable<Prop> and
                (forwarding_query(Prop{})) and
                std::invocable<Prop, execution::env_of_t<Receiver>, Args...>
        COIO_ALWAYS_INLINE decltype(auto) query(const Prop& prop, Args&&... args) const noexcept {
            return prop(execution::get_env(state->receiver), std::forward<Args>(args)...);
        }

        state_base<Receiver>* state;
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<typename Receiver>
    struct when_any_t::state_base {
        template<typename Rcvr>
        state_base(std::size_t total, Rcvr&& rcvr) : total(total), receiver(std::forward<Rcvr>(rcvr)) {}

        state_base(const state_base&) = delete;

        ~state_base() = default;

        auto operator= (const state_base&) -> state_base& = delete;

        virtual auto finish() -> void = 0;

        COIO_ALWAYS_INLINE auto report() -> bool {
            if (done_count++ == 0) {
                source.request_stop();
                return true;
            }
            return false;
        }

        COIO_ALWAYS_INLINE auto arrive() -> void {
            if (++this->ready_count == this->total) {
                this->finish();
            }
        }

        std::size_t total{};
        Receiver receiver{};
        std::atomic<std::size_t> done_count{};
        std::atomic<std::size_t> ready_count{};
        inplace_stop_source source{};
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<execution::receiver Receiver, typename Value, typename Error>
    struct when_any_t::state_value : state_base<Receiver> {
        template<typename Rcvr>
        state_value(std::size_t total, Rcvr&& rcvr) : state_base<Receiver>{total, std::forward<Rcvr>(rcvr)} {}

        COIO_ALWAYS_INLINE auto finish() -> void override {
            switch (result.index()) {
            case 0: {
                execution::set_stopped(std::move(this->receiver));
                break;
            }
            case 1: {
                std::visit([this](auto&& tpl) {
                    std::apply(
                        [this](auto&&... values) {
                            execution::set_value(std::move(this->receiver), std::move(values)...);
                        },
                        tpl
                    );
                }, std::get<1>(result));
                break;
            }
            case 2: {
                execution::set_error(std::move(this->receiver), std::move(std::get<2>(result)));
                break;
            }
            default: unreachable();
            }
        }

        std::variant<std::monostate, Value, Error> result;
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<execution::receiver Receiver, typename Value, typename Error>
    struct when_any_t::receiver {
        using receiver_concept = execution::receiver_t;

        COIO_ALWAYS_INLINE auto get_env() const noexcept -> env<Receiver> {
            return {state};
        }

        template<typename... Args>
        COIO_ALWAYS_INLINE auto set_value(Args&&... args) && noexcept -> void {
            if (state->report()) {
                state->result.template emplace<1>(
                    std::in_place_type<std::tuple<std::decay_t<Args>...>>,
                    std::forward<Args>(args)...
                );
            }
            state->arrive();
        }

        template<typename E>
        COIO_ALWAYS_INLINE auto set_error(E&& error) && noexcept -> void {
            if (state->report()) {
                state->result.template emplace<2>(std::forward<E>(error));
            }
            state->arrive();
        }

        COIO_ALWAYS_INLINE auto set_stopped() && noexcept -> void {
            state->report();
            state->arrive();
        }

        state_value<Receiver, Value, Error>* state;
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<std::size_t... I, execution::receiver Receiver, typename Value, typename Error, execution::sender... Sender>
    struct when_any_t::state<std::index_sequence<I...>, Receiver, Value, Error, Sender...> : state_value<Receiver, Value, Error> {
        using operation_state_concept = execution::operation_state_t;
        using base = state_value<Receiver, Value, Error>;
        using value_type = Value;
        using error_type = Error;
        template<std::size_t J>
        using receiver_at = receiver<Receiver, value_type, error_type>;
        using states_type = std::tuple<execution::connect_result_t<Sender, receiver_at<I>>...>;

        template<typename Rcvr, typename Sndrs>
        state(Rcvr&& rcvr, Sndrs&& when_any_sndrs) :
            base(sizeof...(Sender), std::forward<Rcvr>(rcvr)),
            states{
                detail::elide{
                    execution::connect,
                    std::get<I>(std::forward<Sndrs>(when_any_sndrs)),
                    receiver_at<I>{this}
                }...
            } {}

        state(state&&) = delete;

        COIO_ALWAYS_INLINE auto start() & noexcept -> void {
            (..., execution::start(std::get<I>(this->states)));
        }

        states_type states;
    };

    template<execution::sender... Sender>
    struct when_any_t::sender {
        using sender_concept = execution::sender_t;

        template<execution::receiver Receiver>
        COIO_ALWAYS_INLINE auto connect(Receiver&& receiver) && -> state<
            std::index_sequence_for<Sender...>,
            std::remove_cvref_t<Receiver>,
            execution::value_types_of_t<sender, execution::env_of_t<Receiver>>,
            execution::error_types_of_t<sender, execution::env_of_t<Receiver>>,
            Sender...
        > {
            return {std::forward<Receiver>(receiver), std::move(senders)};
        }

        template<typename Self, typename... Env>
        static consteval auto get_completion_signatures() noexcept {
            static_assert(std::same_as<Self, sender>);
            return detail::merge_completion_signatures_t<execution::completion_signatures_of_t<Sender, Env...>...>{};
        }

        std::tuple<std::remove_cvref_t<Sender>...> senders;
    };


    struct when_any_with_variant_t {
        template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
        COIO_ALWAYS_INLINE COIO_STATIC_CALL_OP auto operator()(Sender&&... sndr) COIO_STATIC_CALL_OP_CONST {
            return execution::into_variant(when_any_t{}(std::forward<Sender>(sndr)...));
        }
    };


    using execution::just;
    using execution::just_error;
    using execution::just_stopped;

    using execution::then;
    using execution::upon_error;
    using execution::upon_stopped;

    using execution::let_value;
    using execution::let_error;
    using execution::let_stopped;

    using execution::schedule;
    using execution::continues_on;
    using execution::starts_on;
    using execution::on;

    using execution::when_all;
    using execution::when_all_with_variant;

    inline constexpr when_any_t when_any{};
    inline constexpr when_any_with_variant_t when_any_with_variant{};


    class async_scope : retain_base<async_scope> {
        friend retain_base;
        friend retain_ptr<async_scope>;
    public:
        class join_sender {
            friend async_scope;
        private:
            class awaiter {
                friend join_sender;
                friend async_scope;
            public:
                awaiter(async_scope& scope) noexcept : scope_(scope) {}

                awaiter(const awaiter&) = delete;

                auto operator= (const awaiter&) -> awaiter& = delete;

                auto await_ready() noexcept -> bool {
                    return scope_.ref_count_.load(std::memory_order_acquire) == 0;
                }

                template<typename Promise>
                auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> bool {
                    coro_ = this_coro;
                    scope_.list_.push(*this);
                    return scope_.ref_count_.fetch_sub(1, std::memory_order_acq_rel) > 1;
                }

                static auto await_resume() noexcept -> void {}

            private:
                async_scope& scope_;
                std::coroutine_handle<> coro_;
                awaiter* next_ = nullptr;
            };

        private:
            join_sender(async_scope& scope) noexcept : scope_(&scope) {}

        public:
            join_sender(const join_sender&) = delete;

            join_sender(join_sender&& other) noexcept : scope_(std::exchange(other.scope_, {})) {};

            auto operator= (const join_sender&) -> join_sender& = delete;

            auto operator= (join_sender&& other) noexcept -> join_sender& {
                scope_ = std::exchange(other.scope_, {});
                return *this;
            }

            auto operator co_await() && noexcept -> awaiter {
                return awaiter{*std::exchange(scope_, nullptr)};
            }

        private:
            async_scope* scope_;
        };

    public:
        async_scope() noexcept : retain_base(1) {}

        async_scope(const async_scope&) = delete;

        ~async_scope() {
            request_stop();
        }

        auto operator= (const async_scope&) -> async_scope& = delete;

        template<execution::sender Sndr>
        COIO_ALWAYS_INLINE auto spawn(Sndr sndr) -> void {
            auto allocator = detail::query_or(get_allocator, execution::get_env(sndr), std::allocator<void>{});
            [](std::allocator_arg_t, auto, auto spawned, retain_ptr<async_scope>) -> detail::fire_and_forget {
                void(co_await std::move(spawned));
            }(std::allocator_arg, std::move(allocator), stop_when(std::move(sndr), stop_source_.get_token()), retain_ptr{this});
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto join() noexcept -> join_sender {
            return join_sender{*this};
        }

        COIO_ALWAYS_INLINE auto request_stop() -> void {
            void(stop_source_.request_stop());
        }

    private:
        auto do_lose() noexcept -> void {
            auto node = list_.pop_all();
            while (node) {
                auto next = node->next_;
                node->coro_.resume();
                node = next;
            }
        }

    private:
        inplace_stop_source stop_source_;
        detail::intrusive_stack<join_sender::awaiter> list_{&join_sender::awaiter::next_};
    };
}