#include <coio/core.h>
#include <coio/steady_timer.h>
#include "common.h"

using namespace std::chrono_literals;

auto foo(coio::io_context& context) -> coio::task<int> {
    coio::steady_timer timer{context};
    co_await timer.async_wait(1s);
    ::println("foo completed");
    co_return 114;
}

auto bar(coio::io_context& context) -> coio::task<int> {
    coio::steady_timer timer{context};
    co_await timer.async_wait(2s);
    ::println("bar completed");
    co_return 514;
}

auto main() -> int {
    coio::io_context context;
    auto tick = std::chrono::steady_clock::now();
    auto [i, j] = coio::sync_wait(coio::when_all(
        foo(context),
        bar(context),
        [&context]() -> coio::task<> {
            co_return context.run();
        }()
    ));
    auto tock = std::chrono::steady_clock::now();
    ::println("result: i = {}, j = {}", i, j); // result: i = 114, j = 514
    ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()); // take: 2000ms
}