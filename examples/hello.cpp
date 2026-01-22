#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include "common.h"

using namespace std::chrono_literals;

auto foo(coio::timed_run_loop::scheduler sched) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(1s);
    ::println("foo completed");
    co_return 114;
}

auto bar(coio::timed_run_loop::scheduler sched) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(2s);
    ::println("bar completed");
    co_return 514;
}

auto main() -> int {
    coio::timed_run_loop context;
    auto tick = std::chrono::steady_clock::now();
    auto [i, j] = coio::this_thread::sync_wait(coio::execution::when_all(
        foo(context.get_scheduler()),
        bar(context.get_scheduler()),
        [&context]() -> coio::task<> {
            context.run();
            co_return;
        }()
    )).value();
    auto tock = std::chrono::steady_clock::now();
    ::println("result: i = {}, j = {}", i, j); // result: i = 114, j = 514
    ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()); // take: 2000ms
}