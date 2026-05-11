#include <thread>
#include <doctest/doctest.h>
#include <coio/core.h>
#include <coio/sync_primitives.h>

namespace {
    struct worker {
        coio::time_loop loop;
        std::jthread thrd;

        auto work() {
            thrd = std::jthread([this] {
                loop.run();
            });
        }
    };
}

TEST_CASE("async_mutex serializes access between waiters") {
    coio::async_mutex mutex;
    worker workers[4];
    std::size_t result = 0;
    constexpr std::size_t n = 16;
    coio::async_scope scope;
    for (std::size_t i = 0; i < n; ++i) {
        auto sched = workers[i % std::ranges::size(workers)].loop.get_scheduler();
        scope.spawn(
            coio::schedule(sched)
            | coio::let_value([&]() noexcept { return mutex.lock_guard(); })
            | coio::then([&result, i](auto) noexcept { result += i; })
        );
    }

    for (auto& worker : workers) worker.work();

    coio::this_thread::sync_wait(scope.join());

    CHECK_EQ(result, 120); // 0 + 1 + ... + 15 == 120
}
