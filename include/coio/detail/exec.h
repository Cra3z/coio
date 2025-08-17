#pragma once
#include "../config.h"
#ifdef COIO_ENABLE_SENDERS
#ifdef COIO_EXECUTION_USE_NVIDIA
#if __has_include(<stdexec/execution.hpp>) // https://github.com/NVIDIA/stdexec
#include <stdexec/execution.hpp>
namespace coio::detail {
    namespace exec = ::stdexec;
}
#else
#error "nvidia/stdexec not found."
#endif
#elif defined(COIO_EXECUTION_USE_BEMAN)
#if __has_include(<beman/execution/execution.hpp>) // https://github.com/bemanproject/execution
#include <beman/execution/execution.hpp>
namespace coio::detail {
    namespace exec = ::beman::execution;
}
#else
#error "bemanproject/execution not found."
#endif
#elif defined(__cpp_lib_senders)
#include <execution>
namespace coio::detail {
    namespace exec = ::std::execution;
}
#else
#error "no suitable C++26 `std::execution` implement library found."
#endif
#endif
#include "co_promise.h"

namespace coio::detail {
#ifdef COIO_ENABLE_SENDERS
    template<typename Promise>
    using enable_await_senders = exec::with_awaitable_senders<Promise>;
    using scheduler_tag = exec::scheduler_t;
    using sender_tag = exec::sender_t;
    using operation_state_tag = exec::operation_state_t;
#else
    class with_awaitable_senders_ {
    protected:
        with_awaitable_senders_() = default;

    public:
        template<typename OtherPromise>
        auto set_continuation(std::coroutine_handle<OtherPromise> h) noexcept -> void {
            static_assert(not std::same_as<OtherPromise, void>);
            contination_ = h;
            if constexpr (requires (OtherPromise& other) { other.unhandled_stopped(); }) {
                stopped_callback_ = +[](void* p) noexcept -> ::std::coroutine_handle<> {
                    return std::coroutine_handle<OtherPromise>::from_address(p).promise().unhandled_stopped();
                };
            }
            else {
                stopped_callback_ = &default_unhandled_stopped;
            }
        }

        auto continuation() const noexcept -> std::coroutine_handle<> {
            return contination_;
        }

        auto unhandled_stopped() noexcept -> std::coroutine_handle<> {
            return stopped_callback_(contination_.address());
        }

    private:
        [[noreturn]]
        static auto default_unhandled_stopped(void*) noexcept -> ::std::coroutine_handle<> {
            ::std::terminate();
        }

        std::coroutine_handle<> contination_;
        std::coroutine_handle<> (*stopped_callback_)(void*) noexcept = &default_unhandled_stopped;
    };

    template<typename Promise>
    using enable_await_senders = with_awaitable_senders_;
    using scheduler_tag = nothing;
    using sender_tag = nothing;
    using operation_state_tag = nothing;
#endif
}