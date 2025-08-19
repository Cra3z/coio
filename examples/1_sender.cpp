#define COIO_ENABLE_SENDERS
#define COIO_EXECUTION_USE_NVIDIA
#include <stdexec/execution.hpp>
#include <coio/core.h>
#include <coio/steady_timer.h>
#include "common.h"

auto main() -> int {
    coio::io_context io_context;
    auto emit_after = [&io_context](int i) -> coio::task<int> {
        coio::steady_timer timer{io_context};
        co_await timer.async_wait(std::chrono::seconds(i));
        co_return co_await stdexec::just(i);
    };

    auto tick = std::chrono::steady_clock::now();
    auto [i, j, k] = stdexec::sync_wait(stdexec::when_all(
        emit_after(1),
        emit_after(2),
        emit_after(3),
        [&io_context]() -> coio::task<> {
            co_return io_context.run();
        }()
    )).value();
    auto tock = std::chrono::steady_clock::now();
    ::println("result: i = {}, j = {}, k = {}", i, j, k); // result: i = 1, j = 2, k = 3
    ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()); // take: 3000ms
}