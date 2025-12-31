#include <thread>
#include <coio/utils/conqueue.h>
#include "../common.h"

namespace {
    std::thread::id main_thread_id = std::this_thread::get_id();

    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        auto thread_id = std::this_thread::get_id();
        auto thread_name = thread_id == main_thread_id ? "main" : (std::stringstream{} << thread_id).str();
        std::clog << std::format("[thread-{}] {}\n", thread_name, std::format(fmt, std::forward<Args>(args)...));
    }

    class worker {
    public:
        worker(int index) : thrd([index, this] {
            debug("runloop-{} run...", index);
            loop.run();
            debug("runloop-{} done...", index);
        }) {}

        worker(const worker&) = delete;

        auto operator=(const worker&) -> worker& = delete;

        ~worker() {
            loop.finish();
        }

        auto scheduler() {
            return loop.get_scheduler();
        }

    private:
        coio::execution::run_loop loop;
        std::jthread thrd;
    };
}

auto main() -> int {
    worker workers[6]{1, 2, 3, 4, 5, 6};
    coio::conqueue<std::string> channel;
    auto writer = [&](coio::execution::scheduler auto sched, std::string_view name, std::initializer_list<std::string_view> datum) -> coio::task<> {
        for (auto str : datum) {
            co_await (channel.emplace(str) | coio::execution::continues_on(sched) | coio::execution::then([&] {
                ::debug("{} writes {}", name, str);
            }));
        }
    };

    auto reader = [&](coio::execution::scheduler auto sched, std::string_view name) -> coio::task<> {
        while (true) {
            auto str = co_await (channel.pop() | coio::execution::continues_on(sched) | coio::execution::then([&](std::string str_) {
                ::debug("{} reads {}", name, str_);
                return str_;
            }));
            if (str == "bye") break;
        }
    };

    auto start_writer = [&writer](coio::execution::scheduler auto sched, std::string_view name, std::initializer_list<std::string_view> datum) {
        return coio::execution::starts_on(sched, writer(sched, name, datum));
    };

    auto start_reader = [&reader](coio::execution::scheduler auto sched, std::string_view name) {
        return coio::execution::starts_on(sched, reader(sched, name));
    };

    coio::execution::sync_wait(coio::execution::when_all(
        start_writer(workers[0].scheduler(), "writer-1", {"1#1", "1#2", "1#3", "1#4", "bye", "bye"}),
        start_writer(workers[1].scheduler(), "writer-2", {"2#1", "2#2", "2#3", "2#4", "2#5", "2#6", "2#7", "bye", "bye"}),
        start_reader(workers[2].scheduler(), "reader-1"),
        start_reader(workers[3].scheduler(), "reader-2"),
        start_reader(workers[4].scheduler(), "reader-3"),
        start_reader(workers[5].scheduler(), "reader-4")
    ));
}