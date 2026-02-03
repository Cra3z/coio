#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include "common.h"

auto job(coio::time_loop::scheduler sched, std::string_view name, std::chrono::seconds timeout) -> coio::task<> {
    coio::timer timer{sched};
    co_await timer.async_wait(timeout);
    ::println("{} completed", name);
}

auto main() -> int {
    using namespace std::chrono_literals;
    coio::time_loop context;
    const auto tick = std::chrono::steady_clock::now();
    coio::async_scope scope;
    scope.spawn(job(context.get_scheduler(), "foo", 2s));
    scope.spawn(job(context.get_scheduler(), "bar", 1s));
    scope.spawn(job(context.get_scheduler(), "qux", 3s));
    context.run();
    coio::this_thread::sync_wait(scope.join()); // wait all sub-tasks completed
    const auto tock = std::chrono::steady_clock::now();
    ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()); // take: 3000ms
}