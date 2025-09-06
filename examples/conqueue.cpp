#include <thread>
#include <stdexec/execution.hpp>
#include <coio/core.h>
#include <coio/utils/conqueue.h>
#include "common.h"

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
        stdexec::run_loop loop;
        std::jthread thrd;
    };
}

auto main() -> int {
    worker workers[6]{1, 2, 3, 4, 5, 6};
    coio::conqueue<std::string> channel;
    auto writer = [&](stdexec::scheduler auto sched, std::string_view name, std::initializer_list<std::string_view> datum) -> coio::task<> {
        for (auto str : datum) {
            co_await (channel.emplace(str) | stdexec::continues_on(sched) | stdexec::then([&] {
                ::debug("{} writes {}", name, str);
            }));
        }
    };

    auto reader = [&](stdexec::scheduler auto sched, std::string_view name) -> coio::task<> {
        while (true) {
            auto str = co_await (channel.pop() | stdexec::continues_on(sched) | stdexec::then([&](std::string str_) {
                ::debug("{} reads {}", name, str_);
                return str_;
            }));
            if (str == "bye") break;
        }
    };

    auto start_writer = [&writer](stdexec::scheduler auto sched, std::string_view name, std::initializer_list<std::string_view> datum) {
        return stdexec::starts_on(sched, writer(sched, name, datum));
    };

    auto start_reader = [&reader](stdexec::scheduler auto sched, std::string_view name) {
        return stdexec::starts_on(sched, reader(sched, name));
    };

    stdexec::sync_wait(stdexec::when_all(
        start_writer(workers[0].scheduler(), "writer-1", {"1#1", "1#2", "1#3", "1#4", "bye", "bye"}),
        start_writer(workers[1].scheduler(), "writer-2", {"2#1", "2#2", "2#3", "2#4", "2#5", "2#6", "2#7", "bye", "bye"}),
        start_reader(workers[2].scheduler(), "reader-1"),
        start_reader(workers[3].scheduler(), "reader-2"),
        start_reader(workers[4].scheduler(), "reader-3"),
        start_reader(workers[5].scheduler(), "reader-4")
    ));
}