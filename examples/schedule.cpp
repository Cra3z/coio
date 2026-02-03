#include <thread>
#include <coio/core.h>
#include <coio/execution_context.h>
#include "common.h"

namespace {
    class worker {
    public:
        worker(std::string_view name) {
            thrd = std::jthread{[name, this] {
                ::debug("worker-{} run...", name);
                loop.run();
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
    using namespace std::string_view_literals;
    worker workers[]{"alice"sv, "bob"sv};
    coio::this_thread::sync_wait(coio::just() | coio::then([] {
        ::debug("in main thread");
    }) | coio::continues_on(workers[0].scheduler()) | coio::then([] {
        ::debug("in worker-alice thread");
    }) | coio::continues_on(workers[1].scheduler()) | coio::then([] {
        ::debug("in worker-bob thread");
    })).value();
}