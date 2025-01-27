#include <coio/core.h>
#include "common.h"

auto main() -> int {
    coio::io_context context;
    coio::sync_wait(coio::when_all(
        [&context]() -> coio::task<> {
            using namespace std::chrono_literals;
            for (std::size_t i = 0; i < 5; ++i) {
                co_await coio::steady_timer{context, 1s}.async_wait();
                ::print(std::clog, "{}", std::string(i + 1, '='));
            }
            ::println(std::clog);
        }(),
        [&context]() -> coio::task<> {
            co_return context.run();
        }()
    ));
}