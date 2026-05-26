#include <thread>
#include <doctest/doctest.h>
#include <coio/core.h>
#include <coio/sync_primitives.h>

namespace {
    class worker {
    public:
        worker() {
            thrd = std::jthread([this] {
                loop.run();
            });
        }

        auto get_scheduler() noexcept {
            return loop.get_scheduler();
        }

    private:
        coio::time_loop loop;
        std::jthread thrd;
        coio::work_guard<coio::time_loop> _{loop};
    };
}

TEST_CASE("async_mutex serializes access between waiters") {
    coio::async_mutex mutex;
    worker workers[4];
    std::size_t result = 0;
    constexpr std::size_t n = 16;
    coio::async_scope scope;
    for (std::size_t i = 0; i < n; ++i) {
        auto sched = workers[i % std::ranges::size(workers)].get_scheduler();
        scope.spawn(
            coio::schedule(sched)
            | coio::let_value([&]() noexcept { return mutex.lock_guard(); })
            | coio::then([&result, i](auto) noexcept { result += i; })
        );
    }

    coio::this_thread::sync_wait(scope.join());

    CHECK_EQ(result, 120); // 0 + 1 + ... + 15 == 120
}
