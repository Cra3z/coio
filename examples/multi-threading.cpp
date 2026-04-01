#include <thread>
#include <coio/core.h>
#include <coio/execution_context.h>
#include "common.h"

auto main() -> int {
    using namespace std::chrono_literals;
    constexpr std::size_t num_threads = 4;
    coio::time_loop context;
    coio::async_scope scope;

    auto sched = context.get_scheduler();
    for (std::size_t i = 0; i < 10; ++i) {
        scope.spawn(sched.schedule() | coio::then([i] {
            std::this_thread::sleep_for(1s); // simulate a long-running task
            ::debug("task-{} done...", i);
        }));
    }

    std::vector<std::jthread> works;
    works.reserve(num_threads);
    const auto tick = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < num_threads; ++i) {
        works.emplace_back([&context] {
            context.run();
        });
    }
    coio::this_thread::sync_wait(scope.join());
    const auto tock = std::chrono::steady_clock::now();
    ::println("take: {}ms", std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count());
}