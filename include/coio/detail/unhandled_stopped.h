#pragma once
#include "concepts.h"

namespace coio::detail {
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
}