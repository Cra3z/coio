#include <thread>
#include <coio/core.h>
#include <coio/sync_primitives.h>
#include "common.h"

template<typename... Args>
auto debug(std::format_string<Args...> fmt, Args&&... args) -> void {
    ::println(std::clog << "[thread-" << std::this_thread::get_id() << "] ", fmt, std::forward<Args>(args)...);
}

auto main() -> int {
    coio::io_context context;
    coio::async_mutex mutex;
    auto task = [&mutex, &context](std::initializer_list<std::string_view> strings) -> coio::task<> {
        using namespace std::chrono_literals;
        auto _ = context.make_work_guard();
        for (std::size_t i = 0; i < 5; ++i) {
            coio::async_lock_guard _ = co_await mutex.make_lock_guard();
            for (auto str : strings) {
                co_await coio::steady_timer{context, 200ms}.async_wait();
                ::debug("{}", str);
            }
        }
    };
    coio::sync_wait(coio::when_all(
        task({"😂", "🤣"}),
        task({"😍", "😘"}),
        task({"🤢", "🤮"}),
        task({"❤️", "💕"}),
        task({"🤦‍♂️", "🤷‍♂️"}),
        [&context]() -> coio::task<> {
            std::vector<std::jthread> thrds;
            for (std::size_t i = 0; i < 3; ++i) {
                thrds.emplace_back(&coio::io_context::run, &context);
            }
            co_return;
        }()
    ));
}