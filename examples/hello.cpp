#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include "common.h"

using namespace std::chrono_literals;

auto job(coio::time_loop::scheduler sched, std::string_view name, int value, std::chrono::seconds timeout) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(timeout);
    ::println("{} completed", name);
    co_return value;
}

auto main() -> int {
    coio::time_loop context;
    coio::async_scope scope;
    auto sched = context.get_scheduler();
    scope.spawn(coio::schedule(sched) | coio::then([]() noexcept {
        ::println("#1");
    }));
    context.run_one();
    context.request_stop();
    scope.spawn(coio::schedule(sched) | coio::then([]() noexcept {
        ::println("#2");
    }));
    context.run_one();
    coio::this_thread::sync_wait(scope.join());
    // const auto tick = std::chrono::steady_clock::now();
    // auto [i, j] = coio::this_thread::sync_wait(coio::when_all(
    //     job(context.get_scheduler(), "foo", 114, 2s),
    //     job(context.get_scheduler(), "bar", 514, 1s),
    //     [&context]() -> coio::task<> {
    //         context.run();
    //         co_return;
    //     }()
    // )).value();
    // const auto tock = std::chrono::steady_clock::now();
    // ::println("result: i = {}, j = {}", i, j); // result: i = 114, j = 514
    // ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()); // take: 2000ms
}
