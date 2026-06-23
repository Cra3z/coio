// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <cstddef>
#include <functional>
#include <stop_token>
#include <utility>
#include <coio/detail/config.h>
#include <coio/detail/concepts.h>
#include <coio/utils/type_traits.h>
#ifdef COIO_EXECUTION_USE_NVIDIA
#if __has_include(<stdexec/execution.hpp>) // https://github.com/NVIDIA/stdexec
#include <stdexec/execution.hpp>
namespace coio::detail {
#ifdef STDEXEC_NAMESPACE
    namespace execution_impl = STDEXEC_NAMESPACE;
#else
    namespace execution_impl = ::stdexec;
#endif
}
#else
#error "nvidia/stdexec not found."
#endif
#elif defined(COIO_EXECUTION_USE_BEMAN)
#if __has_include(<beman/execution/execution.hpp>) // https://github.com/bemanproject/execution
#include <beman/execution/execution.hpp>
namespace coio::detail {
    namespace execution_impl = ::beman::execution;
}
#else
#error "bemanproject/execution not found."
#endif
#elif defined(__cpp_lib_senders)
#include <execution>
namespace coio::detail {
    namespace execution_impl = ::std::execution;
}
#else
#error "no suitable C++26 `std::execution` implement library found."
#endif

namespace coio {
#if defined(COIO_EXECUTION_USE_NVIDIA) or defined(COIO_EXECUTION_USE_BEMAN)
    using detail::execution_impl::forwarding_query_t;
    using detail::execution_impl::get_allocator_t;
    using detail::execution_impl::get_stop_token_t;

    using detail::execution_impl::forwarding_query;
    using detail::execution_impl::get_allocator;
    using detail::execution_impl::get_stop_token;

    namespace this_thread {
        using ::std::this_thread::yield;
        using ::std::this_thread::get_id;
        using ::std::this_thread::sleep_for;
        using ::std::this_thread::sleep_until;
        using detail::execution_impl::sync_wait_t;
        using detail::execution_impl::sync_wait_with_variant_t;
        using detail::execution_impl::sync_wait;
        using detail::execution_impl::sync_wait_with_variant;
    }
#else
    using ::std::forwarding_query_t;
    using ::std::get_allocator_t;
    using ::std::get_stop_token_t;

    using ::std::forwarding_query;
    using ::std::get_allocator;
    using ::std::get_stop_token;

    namespace this_thread = ::std::this_thread;
#endif

    namespace execution {
        using detail::execution_impl::get_domain_t;
        using detail::execution_impl::get_completion_domain_t;
        using detail::execution_impl::get_scheduler_t;
        using detail::execution_impl::get_start_scheduler_t;
        using detail::execution_impl::get_completion_scheduler_t;
        using detail::execution_impl::get_delegation_scheduler_t;
        using detail::execution_impl::get_forward_progress_guarantee_t;
        using detail::execution_impl::get_await_completion_adaptor_t;

        using detail::execution_impl::get_domain;
        using detail::execution_impl::get_completion_domain;
        using detail::execution_impl::get_scheduler;
        using detail::execution_impl::get_start_scheduler;
        using detail::execution_impl::get_completion_scheduler;
        using detail::execution_impl::get_delegation_scheduler;
        using detail::execution_impl::get_forward_progress_guarantee;
        using detail::execution_impl::get_await_completion_adaptor;

        using detail::execution_impl::sender_tag;
        using detail::execution_impl::sender;
        using detail::execution_impl::sender_in;
        using detail::execution_impl::dependent_sender;
        using detail::execution_impl::sends_stopped;

        using detail::execution_impl::receiver_tag;
        using detail::execution_impl::receiver;

        using detail::execution_impl::operation_state_tag;
        using detail::execution_impl::operation_state;

        using detail::execution_impl::scheduler_tag;
        using detail::execution_impl::schedule_result_t;
        using detail::execution_impl::scheduler;

        using detail::execution_impl::connect_t;
        using detail::execution_impl::connect_result_t;
        using detail::execution_impl::start_t;
        using detail::execution_impl::connect;
        using detail::execution_impl::start;

        using detail::execution_impl::set_value_t;
        using detail::execution_impl::set_error_t;
        using detail::execution_impl::set_stopped_t;
        using detail::execution_impl::set_value;
        using detail::execution_impl::set_error;
        using detail::execution_impl::set_stopped;

        using detail::execution_impl::completion_signatures;
        using detail::execution_impl::completion_signatures_of_t;
        using detail::execution_impl::value_types_of_t;
        using detail::execution_impl::error_types_of_t;
        using detail::execution_impl::get_completion_signatures;

        using detail::execution_impl::forward_progress_guarantee;

        using detail::execution_impl::tag_of_t;

        using detail::execution_impl::env;
        using detail::execution_impl::prop;
        using detail::execution_impl::env_of_t;
        using detail::execution_impl::get_env_t;
        using detail::execution_impl::get_env;
        using detail::execution_impl::write_env;
        using detail::execution_impl::read_env;

        using detail::execution_impl::default_domain;
        using detail::execution_impl::indeterminate_domain;
        using detail::execution_impl::apply_sender;
        using detail::execution_impl::transform_sender;

        using detail::execution_impl::sender_adaptor_closure;

        using detail::execution_impl::just_t;
        using detail::execution_impl::just_error_t;
        using detail::execution_impl::just_stopped_t;
        using detail::execution_impl::just;
        using detail::execution_impl::just_error;
        using detail::execution_impl::just_stopped;

        using detail::execution_impl::then_t;
        using detail::execution_impl::upon_error_t;
        using detail::execution_impl::upon_stopped_t;
        using detail::execution_impl::then;
        using detail::execution_impl::upon_error;
        using detail::execution_impl::upon_stopped;

        using detail::execution_impl::let_value_t;
        using detail::execution_impl::let_error_t;
        using detail::execution_impl::let_stopped_t;
        using detail::execution_impl::let_value;
        using detail::execution_impl::let_error;
        using detail::execution_impl::let_stopped;

        using detail::execution_impl::when_all_t;
        using detail::execution_impl::when_all_with_variant_t;
        using detail::execution_impl::when_all;
        using detail::execution_impl::when_all_with_variant;

        using detail::execution_impl::into_variant_t;
        using detail::execution_impl::stopped_as_error_t;
        using detail::execution_impl::stopped_as_optional_t;
        using detail::execution_impl::into_variant;
        using detail::execution_impl::unstoppable;
        using detail::execution_impl::stopped_as_error;
        using detail::execution_impl::stopped_as_optional;

        using detail::execution_impl::schedule_t;
        using detail::execution_impl::affine_t;
        using detail::execution_impl::schedule_from_t;
        using detail::execution_impl::continues_on_t;
        using detail::execution_impl::starts_on_t;
        using detail::execution_impl::on_t;
        using detail::execution_impl::schedule;
        using detail::execution_impl::affine;
        using detail::execution_impl::schedule_from;
        using detail::execution_impl::continues_on;
        using detail::execution_impl::starts_on;
        using detail::execution_impl::on;

        using detail::execution_impl::with_awaitable_senders;
        using detail::execution_impl::as_awaitable_t;
        using detail::execution_impl::as_awaitable;

        using detail::execution_impl::inline_scheduler;
        using detail::execution_impl::run_loop;

        using detail::execution_impl::scope_association;
        using detail::execution_impl::scope_token;
        using detail::execution_impl::associate_t;
        using detail::execution_impl::spawn_t;
        using detail::execution_impl::spawn_future_t;
        using detail::execution_impl::counting_scope;
        using detail::execution_impl::simple_counting_scope;
        using detail::execution_impl::associate;
        using detail::execution_impl::spawn;
        using detail::execution_impl::spawn_future;
    }

    namespace detail {
        struct io_scheduler_tag : execution::scheduler_tag {};

        template<typename>
        struct is_set_value : std::false_type {};

        template<typename... Args>
        struct is_set_value<execution::set_value_t(Args...)> : std::true_type {};

        template<typename>
        struct is_set_error : std::false_type {};

        template<typename E>
        struct is_set_error<execution::set_error_t(E)> : std::true_type {};

        template<typename T>
        using is_set_stopped = std::is_same<T, execution::set_stopped_t()>;

        template<typename T>
        inline constexpr bool is_set_value_v = is_set_value<T>::value;

        template<typename T>
        inline constexpr bool is_set_error_v = is_set_error<T>::value;

        template<typename T>
        inline constexpr bool is_set_stopped_v = is_set_stopped<T>::value;

        template<typename... Args>
        struct set_value_helper {
            using type = execution::set_value_t(Args...);
        };

        template<>
        struct set_value_helper<void> {
            using type = execution::set_value_t();
        };

        template<typename... Args>
        using set_value_t = typename set_value_helper<Args...>::type;

        template<typename Prop, typename Env, typename Default>
        [[nodiscard]]
        auto query_or(const Prop& prop, const Env& env, Default default_value) noexcept {
            if constexpr (requires { prop(env); }) {
                return prop(env);
            }
            else {
                return std::move(default_value);
            }
        }

        COIO_ALWAYS_INLINE auto get_suitable_start_scheduler(const auto& env) noexcept {
            if constexpr (requires { execution::get_start_scheduler(env); }) {
                return execution::get_start_scheduler(env);
            }
            else if constexpr (requires { execution::get_scheduler(env); }) {
                return execution::get_scheduler(env);
            }
            else {
                return execution::inline_scheduler{};
            }
        }

        COIO_ALWAYS_INLINE auto get_suitable_allocator(const auto& env) noexcept {
            return detail::query_or(get_allocator, env, std::allocator<std::byte>{});
        }

        template<typename Env>
        struct fwd_env_t {
            template<typename Prop, typename... Args>
                requires std::default_initializable<Prop> and (forwarding_query(Prop{}))
                    and std::invocable<Prop, Env, Args...>
            COIO_ALWAYS_INLINE decltype(auto) query(const Prop& prop, Args&&... args) const noexcept {
                return prop(inner, std::forward<Args>(args)...);
            }

            Env inner;
        };

        template<typename Env>
        COIO_ALWAYS_INLINE auto fwd_env(fwd_env_t<Env> env) noexcept -> fwd_env_t<Env> {
            return env;
        }

        template<typename Env>
        COIO_ALWAYS_INLINE auto fwd_env(Env env) noexcept -> fwd_env_t<Env> {
            return fwd_env_t<Env>{std::move(env)};
        }

        template<typename Env1, typename Env2>
        class join_env_t {
        public:
            join_env_t(Env1 env1, Env2 env2) noexcept : env1(std::move(env1)), env2(std::move(env2)) {}

            template<typename Q, typename... Args>
                requires requires(const Env1& env1, const Q& q, Args&&... args) {
                    env1.query(q, ::std::forward<Args>(args)...);
                }
            auto query(const Q& query, Args&&... args) const noexcept -> decltype(auto) {
                return env1.query(query, ::std::forward<Args>(args)...);
            }

            template<typename Q, typename... Args>
                requires(
                    not requires(const Env1& env1, const Q& q, Args&&... args) {
                        env1.query(q, ::std::forward<Args>(args)...);
                    } and
                    requires(const Env2& env2, const Q& query, Args&&... args) {
                        env2.query(query, ::std::forward<Args>(args)...);
                    })
            auto query(const Q& query, Args&&... args) const noexcept -> decltype(auto) {
                return env2.query(query, ::std::forward<Args>(args)...);
            }

        private:
            Env1 env1;
            Env2 env2;
        };

        template<typename Sched1, typename Sched2>
        concept compatible_with_impl = requires (const Sched1& sched1, const Sched2& sched2) {
            { sched1 == sched2 } noexcept -> boolean_testable;
            { sched1 != sched2 } noexcept -> boolean_testable;
        };

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

        template<typename Sndr, typename Env>
        using completion_signature_traits_of_t = completion_signature_helper<
            type_list<>,
            execution::completion_signatures_of_t<Sndr, Env>
        >;

        template<template<typename> typename>
        struct check_type_alias_exists;

        template<typename StopToken>
        struct stoppable_token_traits;

        template<typename StopToken> requires requires {
            typename check_type_alias_exists<StopToken::template callback_type>;
        }
        struct stoppable_token_traits<StopToken> {
            template<typename Fn>
            using callback_type = typename StopToken::template callback_type<Fn>;
        };

        template<>
        struct stoppable_token_traits<std::stop_token> {
            template<typename Fn>
            using callback_type = std::stop_callback<Fn>;
        };
    }

    template<typename StopToken, typename Callback>
    using stop_callback_for_t = typename detail::stoppable_token_traits<StopToken>::template callback_type<Callback>;

    template<typename StopToken>
    concept stoppable_token = requires(const StopToken& token) {
        typename detail::check_type_alias_exists<detail::stoppable_token_traits<StopToken>::template callback_type>;
        { token.stop_requested() } noexcept -> boolean_testable;
        { token.stop_possible() } noexcept -> boolean_testable;
        { StopToken(token) } noexcept;
    } and std::copyable<StopToken> and std::equality_comparable<StopToken>;

    template<typename Source>
    concept stoppable_source = requires(Source& src, const Source& csrc) {
        { csrc.get_token() } -> stoppable_token;
        { csrc.stop_requested() } noexcept -> boolean_testable;
        { src.request_stop() } -> boolean_testable;
    };

    template<typename Token>
    concept unstoppable_token = stoppable_token<Token> and requires(const Token tok) {
        requires std::bool_constant<not Token::stop_possible()>::value;
    };

    template<typename Env>
    using stop_token_of_t = std::decay_t<std::invoke_result_t<get_stop_token_t, Env>>;

    using detail::execution_impl::never_stop_token;
    using detail::execution_impl::inplace_stop_source;
    using detail::execution_impl::inplace_stop_token;
    using detail::execution_impl::inplace_stop_callback;

    template<typename Sched, typename Env>
    concept infallible_scheduler =
        execution::scheduler<Sched> and
        (std::same_as<
                execution::completion_signatures<execution::set_value_t()>,
                execution::completion_signatures_of_t<execution::schedule_result_t<Sched>, Env>> or
            (not unstoppable_token<stop_token_of_t<Env>> and
                (std::same_as<
                        execution::completion_signatures<execution::set_value_t(),
                                                         execution::set_stopped_t()>,
                        execution::completion_signatures_of_t<execution::schedule_result_t<Sched>, Env>> or
                    std::same_as<
                        execution::completion_signatures<execution::set_stopped_t(),
                                                         execution::set_value_t()>,
                        execution::completion_signatures_of_t<execution::schedule_result_t<Sched>, Env>>)));

    template<typename Scheduler>
    concept timed_scheduler = execution::scheduler<Scheduler> and requires (Scheduler&& sch) {
        { static_cast<Scheduler&&>(sch).now() } -> specialization_of<std::chrono::time_point>;
        { static_cast<Scheduler&&>(sch).schedule_after(static_cast<Scheduler&&>(sch).now().time_since_epoch()) } -> execution::sender;
        { static_cast<Scheduler&&>(sch).schedule_at(static_cast<Scheduler&&>(sch).now()) } -> execution::sender;
    };

    template<typename Scheduler>
    concept io_scheduler = execution::scheduler<Scheduler> and
        std::derived_from<typename std::remove_cvref_t<Scheduler>::scheduler_concept, detail::io_scheduler_tag>;

    template<typename Sched1, typename Sched2>
    concept compatible_scheduler =
        execution::scheduler<Sched1> and
        execution::scheduler<Sched2> and
        detail::compatible_with_impl<Sched1, Sched2> and
        detail::compatible_with_impl<Sched2, Sched1>;

    struct append_fallback_env_t {
        template<typename Rcvr, typename FallbackEnv>
        struct receiver {
            using receiver_concept = execution::receiver_tag;

            template<typename... Args>
            COIO_ALWAYS_INLINE auto set_value(Args&&... args) && noexcept -> void {
                execution::set_value(std::move(rcvr), std::forward<Args>(args)...);
            }

            template<typename E>
            COIO_ALWAYS_INLINE auto set_error(E&& e) && noexcept -> void {
                execution::set_error(std::move(rcvr), std::forward<E>(e));
            }

            COIO_ALWAYS_INLINE auto set_stopped() && noexcept -> void {
                execution::set_stopped(std::move(rcvr));
            }

            COIO_ALWAYS_INLINE auto get_env() const noexcept {
                return detail::join_env_t{execution::get_env(rcvr), fallback_env};
            }

            Rcvr rcvr;
            FallbackEnv fallback_env;
        };

        template<typename Child, typename FallbackEnv>
        struct sender {
            using sender_concept = execution::sender_tag;

            template<typename Rcvr>
            COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                return execution::connect(std::move(child), receiver<Rcvr, FallbackEnv>{std::move(rcvr), std::move(fallback_env)});
            }

            template<similar_to<sender>> requires (not execution::dependent_sender<Child>)
            static consteval auto get_completion_signatures() {
                return execution::get_completion_signatures<Child>();
            }

            template<similar_to<sender>, typename Env>
            static consteval auto get_completion_signatures() {
                return execution::get_completion_signatures<Child, detail::join_env_t<Env, FallbackEnv>>();
            }

            COIO_ALWAYS_INLINE auto get_env() const noexcept {
                return execution::get_env(child);
            }

            Child child;
            FallbackEnv fallback_env;
        };

        template<typename Sndr, typename Env>
        [[nodiscard]]
        COIO_ALWAYS_INLINE COIO_STATIC_CALL_OP auto operator() (Sndr sndr, Env env) COIO_STATIC_CALL_OP_CONST noexcept {
            return sender<Sndr, Env>{std::move(sndr), std::move(env)};
        }
    };

    inline constexpr append_fallback_env_t append_fallback_env{};
}
