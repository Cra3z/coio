#pragma once
#include "config.h"
#ifdef COIO_ENABLE_SENDERS
#ifdef COIO_EXECUTION_USE_NVIDIA
#if __has_include(<stdexec/execution.hpp>) // https://github.com/NVIDIA/stdexec
#include <stdexec/execution.hpp>
namespace coio {
#ifdef STDEXEC_NAMESPACE
    namespace execution = STDEXEC_NAMESPACE;
#else
    namespace execution = ::stdexec;
#endif
}
#else
#error "nvidia/stdexec not found."
#endif
#elif defined(COIO_EXECUTION_USE_BEMAN)
#if __has_include(<beman/execution/execution.hpp>) // https://github.com/bemanproject/execution
#include <beman/execution/execution.hpp>
namespace coio {
    namespace execution = ::beman::execution;
}
#else
#error "bemanproject/execution not found."
#endif
#elif defined(__cpp_lib_senders)
#include <execution>
namespace coio {
    namespace execution = ::std::execution;
}
#else
#error "no suitable C++26 `std::execution` implement library found."
#endif
#endif

namespace coio::detail {
#ifdef COIO_ENABLE_SENDERS
    using scheduler_tag = execution::scheduler_t;
    using sender_tag = execution::sender_t;
    using operation_state_tag = execution::operation_state_t;
#else
    struct scheduler_tag {};
    struct sender_tag {};
    struct operation_state_tag {};
#endif
    struct io_scheduler_tag : scheduler_tag {};
}