#pragma once
#include "config.h"
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
        using detail::execution_impl::get_scheduler_t;
        using detail::execution_impl::get_delegatee_scheduler_t;
        using detail::execution_impl::get_forward_progress_guarantee_t;
        using detail::execution_impl::get_completion_scheduler_t;
        using detail::execution_impl::get_env_t;
        using detail::execution_impl::get_completion_signatures_t;

        using detail::execution_impl::get_domain;
        using detail::execution_impl::get_scheduler;
        using detail::execution_impl::get_delegatee_scheduler;
        using detail::execution_impl::get_forward_progress_guarantee;
        using detail::execution_impl::get_completion_scheduler;
        using detail::execution_impl::get_env;
        using detail::execution_impl::get_completion_signatures;

        using detail::execution_impl::sender_t;
        using detail::execution_impl::sender;
        using detail::execution_impl::sender_in;
        using detail::execution_impl::sender_to;
        using detail::execution_impl::dependent_sender;
        using detail::execution_impl::enable_sender;

        using detail::execution_impl::receiver_t;
        using detail::execution_impl::receiver;
        using detail::execution_impl::receiver_of;

        using detail::execution_impl::operation_state_t;
        using detail::execution_impl::operation_state;

        using detail::execution_impl::scheduler_t;
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

        using detail::execution_impl::env;
        using detail::execution_impl::completion_signatures;
        using detail::execution_impl::forward_progress_guarantee;
        using detail::execution_impl::prop;
        using detail::execution_impl::env_of_t;
        using detail::execution_impl::completion_signatures_of_t;
        using detail::execution_impl::value_types_of_t;
        using detail::execution_impl::error_types_of_t;
        using detail::execution_impl::tag_of_t;
        using detail::execution_impl::write_env;
        using detail::execution_impl::read_env;
        using detail::execution_impl::sends_stopped;

        using detail::execution_impl::default_domain;
        using detail::execution_impl::apply_sender_t;
        using detail::execution_impl::transform_sender_t;
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
        using detail::execution_impl::stopped_as_error;
        using detail::execution_impl::stopped_as_optional;

        using detail::execution_impl::schedule_t;
        using detail::execution_impl::schedule_from_t;
        using detail::execution_impl::continues_on_t;
        using detail::execution_impl::starts_on_t;
        using detail::execution_impl::on_t;
        using detail::execution_impl::schedule;
        using detail::execution_impl::schedule_from;
        using detail::execution_impl::continues_on;
        using detail::execution_impl::starts_on;
        using detail::execution_impl::on;

        using detail::execution_impl::bulk_t;
        using detail::execution_impl::bulk_chunked_t;
        using detail::execution_impl::bulk_unchunked_t;
        using detail::execution_impl::bulk;
        using detail::execution_impl::bulk_chunked;
        using detail::execution_impl::bulk_unchunked;

        using detail::execution_impl::with_awaitable_senders;
        using detail::execution_impl::as_awaitable_t;
        using detail::execution_impl::as_awaitable;

        using detail::execution_impl::inline_scheduler;
        using detail::execution_impl::run_loop;
    }

    namespace detail {
        struct io_scheduler_t : execution::scheduler_t {};

        template<typename>
        struct is_set_value : std::false_type {};

        template<typename... Args>
        struct is_set_value<execution::set_value_t(Args...)> : std::true_type {};

        template<typename>
        struct is_set_error : std::false_type {};

        template<typename E>
        struct is_set_error<execution::set_error_t(E)> : std::true_type {};


    }
}