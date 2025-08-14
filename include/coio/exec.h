#pragma once
#include "config.h"
#ifdef COIO_ENABLE_SENDERS
#ifdef __cpp_lib_senders
#include <execution>
namespace coio::detail {
    namespace exec = ::std::execution;
}
#elif __has_include(<stdexec/execution.hpp>) // https://github.com/NVIDIA/stdexec
#include <stdexec/execution.hpp>
namespace coio::detail {
    namespace exec = ::stdexec;
}
#elif __has_include(<beman/execution/execution.hpp>) // https://github.com/bemanproject/execution
#include <beman/execution/execution.hpp>
namespace coio::detail {
    namespace exec = ::beman::execution;
}
#else
#error "no suitable C++26 `std::execution` implement library found."
#endif
#endif
#include "detail/co_promise.h"

namespace coio::detail {
#ifdef COIO_ENABLE_SENDERS
    template<typename Promise>
    using enable_await_senders = exec::with_awaitable_senders<Promise>;
#else
    template<typename Promise>
    using enable_await_senders = nothing;
#endif
}