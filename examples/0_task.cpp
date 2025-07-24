#include <coio/core.h>
#include <coio/steady_timer.h>
#include "common.h"

using namespace std::chrono_literals;

auto foo(coio::io_context& context) -> coio::task<int> {
    coio::steady_timer timer{context};
    co_await timer.async_wait(1s);
    ::println("foo completed");
    co_return 123;
}

auto bar(coio::io_context& context) -> coio::task<int> {
    coio::steady_timer timer{context};
    co_await timer.async_wait(2s);
    ::println("bar completed");
    co_return 456;
}

auto main() -> int {
    coio::io_context context;
    coio::sync_wait(coio::when_all(
        foo(context),
        bar(context),
        [&context]() -> coio::task<> {
            co_return context.run();
        }()
    ));
}