# time_loop

`coio::time_loop` is coio's portable execution context: a manually driven event loop with a timer queue and no I/O backend. It runs everywhere the library compiles and is the natural choice for worker threads, testing, and timer-driven logic.

Header: `#include <coio/execution_context.h>`

## Overview

`time_loop` implements the full [shared execution-context model](contexts.md): MPSC threading, `run`/`poll` driving, work tracking, `request_stop`, and a scheduler with `schedule()`, `schedule_at()`, `schedule_after()` and `now()`. What distinguishes it from the I/O contexts is only what it *lacks*: there is no `make_io_object`, so it cannot host sockets, files or pipes.

When idle with work outstanding, `run()`/`run_one()` block on an internal semaphore with a deadline taken from the earliest queued timer — the loop wakes exactly when the next timer is due or when another thread submits work.

## Synopsis

```cpp
namespace coio {
    class time_loop {
    public:
        struct scheduler;   // models execution::scheduler; see below

        template<typename T = void, typename Alloc = void>
        using task = coio::task<T, Alloc, scheduler>;

        explicit time_loop(std::pmr::memory_resource& resource = *std::pmr::get_default_resource()) noexcept;
        time_loop(const time_loop&) = delete;
        ~time_loop();
        auto operator= (const time_loop&) -> time_loop& = delete;

        [[nodiscard]] auto get_scheduler() noexcept -> scheduler;
        [[nodiscard]] auto get_allocator() const noexcept -> std::pmr::polymorphic_allocator<>;

        auto request_stop() -> void;

        auto work_started() noexcept -> void;
        auto work_finished() noexcept -> void;

        auto run() -> std::size_t;
        auto run_one() -> bool;
        auto poll() -> std::size_t;
        auto poll_one() -> bool;
    };

    struct time_loop::scheduler {
        using scheduler_concept = execution::scheduler_tag;

        [[nodiscard]] static auto now() noexcept -> std::chrono::steady_clock::time_point;

        [[nodiscard]] auto schedule() const noexcept -> /*sender of ()*/;
        [[nodiscard]] auto schedule_at(std::chrono::steady_clock::time_point deadline) const noexcept
            -> /*sender of ()*/;
        template<typename Rep, typename Period>
        [[nodiscard]] auto schedule_after(std::chrono::duration<Rep, Period> duration) const noexcept
            -> /*sender of ()*/;

        [[nodiscard]] auto context() const noexcept -> time_loop&;

        friend auto operator== (const scheduler&, const scheduler&) -> bool;
    };
}
```

## API Reference

### Construction / destruction

```cpp
explicit time_loop(std::pmr::memory_resource& resource = *std::pmr::get_default_resource()) noexcept;
~time_loop();
```

Construction takes an optional `std::pmr::memory_resource` backing the loop's internal allocations, defaulting to `std::pmr::get_default_resource()`; the resource must outlive the context. The context is neither copyable nor movable. Destroying a `time_loop` with outstanding work calls `std::terminate()` — see [destructor semantics](contexts.md#destructor).

### `scheduler`

```cpp
struct scheduler;
[[nodiscard]] auto get_scheduler() noexcept -> scheduler;
```

`time_loop::scheduler` provides exactly the [common scheduler operations](contexts.md#scheduler-operations): `schedule()`, `schedule_at()`, `schedule_after()`, static `now()`, `context()`, the `get_forward_progress_guarantee` (`parallel`) and `get_allocator` queries, and equality (equal iff same `time_loop`). Its senders complete on the loop's consumer thread and advertise this via `get_completion_scheduler` for all three CPOs.

`schedule_at`/`schedule_after` senders are cancellable both by the receiver's stop token and by `time_loop::request_stop()`.

### `task` alias

```cpp
template<typename T = void, typename Alloc = void>
using task = coio::task<T, Alloc, scheduler>;
```

A [task](../coroutines/task.md) affine to a `time_loop`: awaited senders always resume the coroutine on the loop's consumer thread, with no type-erasure overhead compared to the default `coio::task<T>`. Start one with `starts_on(loop.get_scheduler(), ...)` or by awaiting it from another task on the same loop.

### Driving, work tracking, stopping

`run()`, `run_one()`, `poll()`, `poll_one()`, `work_started()`, `work_finished()` and `request_stop()` behave exactly as specified on the [contexts page](contexts.md#driving-the-context). In short: one consumer thread at a time; `run()` returns the number of completions processed once no work remains; `request_stop()` cancels outstanding timed waits so the loop drains.

## Example

Two timed jobs racing on one loop, driven from the main thread (adapted from `examples/hello.cpp`):

```cpp
#include <chrono>
#include <iostream>
#include <coio/core.h>
#include <coio/execution_context.h>

using namespace std::chrono_literals;

auto job(coio::time_loop::scheduler sched, std::string_view name, int value,
         std::chrono::seconds timeout) -> coio::task<int> {
    co_await sched.schedule_after(timeout);   // timed wait on the loop
    std::cout << name << " completed\n";
    co_return value;
}

auto main() -> int {
    coio::time_loop context;
    const auto tick = std::chrono::steady_clock::now();
    auto [i, j] = coio::this_thread::sync_wait(coio::when_all(
        job(context.get_scheduler(), "foo", 114, 2s),
        job(context.get_scheduler(), "bar", 514, 1s),
        [](coio::time_loop& ctx) -> coio::task<> {   // drive the loop as a third branch
            ctx.run();
            co_return;
        }(context)
    )).value();
    const auto tock = std::chrono::steady_clock::now();
    std::cout << "result: i = " << i << ", j = " << j << '\n';           // 114, 514
    std::cout << "took: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(tock - tick).count()
              << "ms\n";                                                  // ~2000ms
}
```

The two jobs wait concurrently — total time is the maximum (2s), not the sum. The third `when_all` branch runs `context.run()` on the `sync_wait` thread, making it the loop's consumer; `run()` returns once both timers have fired and no work remains.

## See also

- [Execution contexts](contexts.md) — the shared model in full
- [work_guard](work-guard.md) — keep `run()` alive without pending timers
- [timer](../utils/timer.md) — a scheduler-bound timer utility
- [task](../coroutines/task.md)
