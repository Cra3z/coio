// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <coio/task.h> // IWYU pragma: keep
#include <coio/execution_context.h>
#include <coio/detail/execution.h>
#include <coio/detail/atomic_intrusive_stack.h>
#include <coio/utils/async_scope.h> // IWYU pragma: keep
#include <coio/utils/variant_sender.h> // IWYU pragma: keep
#include <coio/utils/when_any.h> // IWYU pragma: keep
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio {
    using execution::scheduler;
    using execution::sender;
    using execution::receiver;
    using execution::operation_state;

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

    inline constexpr auto read_scheduler = []() noexcept {
        return execution::read_env(execution::get_scheduler);
    };

    inline constexpr auto read_start_scheduler = []() noexcept {
        return execution::read_env(execution::get_start_scheduler);
    };

    inline constexpr auto read_allocator = []() noexcept {
        return execution::read_env(get_allocator);
    };

    inline constexpr auto read_stop_token = []() noexcept {
        return execution::read_env(get_stop_token);
    };
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
