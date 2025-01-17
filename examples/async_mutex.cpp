#include <coio/core.h>
#include <coio/utils/sync_primitives.h>
#include <coio/timer.h>
#include "common.h"

auto main() -> int {
    coio::io_context context;
    coio::async_mutex mutex;
    coio::sync_wait(coio::when_all(
        [&mutex, &context]() -> coio::task<> {
            using namespace std::chrono_literals;
            for (std::size_t i = 0; i < 5; ++i) {
                coio::async_lock_guard _ = co_await mutex.make_lock_guard();
                ::print(std::clog, "🤣");
                co_await coio::steady_timer{context, 500ms}.async_wait();
                ::print(std::clog, "👉 ");
                co_await coio::steady_timer{context, 500ms}.async_wait();
            }
        }(),
        [&mutex, &context]() -> coio::task<> {
            using namespace std::chrono_literals;
            for (std::size_t i = 0; i < 5; ++i) {
                coio::async_lock_guard _ = co_await mutex.make_lock_guard();
                ::print(std::clog, "😂");
                co_await coio::steady_timer{context, 500ms}.async_wait();
                ::print(std::clog, "👌 ");
                co_await coio::steady_timer{context, 500ms}.async_wait();
            }
        }(),
        [&context]() -> coio::task<> {
            co_return context.run();
        }()
    ));
}