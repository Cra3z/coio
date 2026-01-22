#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include "../common.h"

auto main() -> int {
    coio::timed_run_loop context;
    auto emit_after = [&context](int i) -> coio::task<int> {
        coio::timer timer{context.get_scheduler()};
        co_await timer.async_wait(std::chrono::seconds(i));
        co_return co_await coio::execution::just(i);
    };

    const auto tick = std::chrono::steady_clock::now();
    auto [i, j, k] = coio::this_thread::sync_wait(coio::execution::when_all(
        emit_after(1),
        emit_after(2),
        emit_after(3),
        [&context]() -> coio::task<> {
            context.run();
            co_return;
        }()
    )).value();
    const auto tock = std::chrono::steady_clock::now();
    ::println("result: i = {}, j = {}, k = {}", i, j, k); // result: i = 1, j = 2, k = 3
    ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()); // take: 3000ms
}