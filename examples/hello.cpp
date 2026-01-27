#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include "common.h"

using namespace std::chrono_literals;

auto foo(coio::time_loop::scheduler sched) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(1s);
    ::println("foo completed");
    co_return 114;
}

auto bar(coio::time_loop::scheduler sched) -> coio::task<int> {
    coio::inplace_stop_source source;
    coio::timer timer{sched};
    co_await coio::stop_when(timer.async_wait(2s), source.get_token());
    ::println("bar completed");
    co_return 514;
}

auto main() -> int {
    coio::time_loop context;
    auto tick = std::chrono::steady_clock::now();
    auto [i] = std::get<1>(coio::this_thread::sync_wait_with_variant(coio::when_any(
        bar(context.get_scheduler()),
        foo(context.get_scheduler()),
        [&context]() -> coio::task<> {
            context.run();
            co_return;
        }()
    )).value());
    auto tock = std::chrono::steady_clock::now();
    ::println("result: i = {}", i); // result: i = 114, j = 514
    ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()); // take: 2000ms
}