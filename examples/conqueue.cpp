#include <thread>
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/conqueue.h>
#include "common.h"

namespace {
    class worker {
    public:
        worker(int index) {
            thrd = std::jthread{[index, this] {
                debug("runloop-{} run...", index);
                loop.run();
                debug("runloop-{} done...", index);
            }};
        }

        worker(const worker&) = delete;

        auto operator=(const worker&) -> worker& = delete;

        auto scheduler() {
            return loop.get_scheduler();
        }

    private:
        coio::time_loop loop;
        std::jthread thrd;
        coio::work_guard<coio::time_loop> _{loop};
    };
}

auto main() -> int {
    worker workers[6]{1, 2, 3, 4, 5, 6};
    coio::conqueue<std::string> channel;
    auto writer = [&](coio::scheduler auto sched, std::string_view name, std::initializer_list<std::string_view> datum) -> coio::task<> {
        for (auto str : datum) {
            co_await coio::then(channel.emplace(str) | coio::continues_on(sched), [&] {
                ::debug("{} writes {}", name, str);
            });
        }
    };

    auto reader = [&](coio::scheduler auto sched, std::string_view name) -> coio::task<> {
        while (true) {
            auto str = co_await coio::then(channel.pop() | coio::continues_on(sched), [&](std::string str_) {
                ::debug("{} reads {}", name, str_);
                return str_;
            });
            if (str == "bye") break;
        }
    };

    auto start_writer = [&writer](coio::scheduler auto sched, std::string_view name, std::initializer_list<std::string_view> datum) {
        return coio::starts_on(sched, writer(sched, name, datum));
    };

    auto start_reader = [&reader](coio::scheduler auto sched, std::string_view name) {
        return coio::starts_on(sched, reader(sched, name));
    };

    coio::this_thread::sync_wait(coio::when_all(
        start_writer(workers[0].scheduler(), "writer-1", {"1#1", "1#2", "1#3", "1#4", "bye", "bye"}),
        start_writer(workers[1].scheduler(), "writer-2", {"2#1", "2#2", "2#3", "2#4", "2#5", "2#6", "2#7", "bye", "bye"}),
        start_reader(workers[2].scheduler(), "reader-1"),
        start_reader(workers[3].scheduler(), "reader-2"),
        start_reader(workers[4].scheduler(), "reader-3"),
        start_reader(workers[5].scheduler(), "reader-4")
    )).value();

    ::debug("==");
}