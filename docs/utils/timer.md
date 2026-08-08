# timer

`coio::timer<Scheduler>` is a lightweight waiting facility bound to a *timed scheduler* (any scheduler providing `now()`, `schedule_after()`, and `schedule_at()` — every coio execution context qualifies). It packages the scheduler's timed-schedule senders together with a cancellation source, so a group of pending waits can be cancelled with one call.

Header: `#include <coio/utils/timer.h>`

## Overview

A `timer` does not own a thread or a queue — the associated execution context's timer queue does the actual timekeeping, and wait completions are delivered like any other scheduled work, on the context's consumer thread. `timer` adds:

- `async_wait(duration)` / `async_wait_until(time_point)` — senders that complete when the time arrives;
- `cancel()` — requests stop on every wait created from this timer.

`timer::async_wait(d)` is exactly `stop_when(scheduler.schedule_after(d), timer's-token)`; if you do not need `cancel()`, using `schedule_after`/`schedule_at` directly is equivalent.

## Synopsis

```cpp
namespace coio {
    template<typename Scheduler>
    concept timed_scheduler = execution::scheduler<Scheduler> and requires (Scheduler&& sch) {
        { sch.now() } -> /* std::chrono::time_point */;
        { sch.schedule_after(duration) } -> execution::sender;
        { sch.schedule_at(time_point) } -> execution::sender;
    };

    template<timed_scheduler Scheduler>
    class timer {
    public:
        using scheduler  = Scheduler;
        using time_point = decltype(std::declval<Scheduler>().now());
        using clock      = typename time_point::clock;
        using duration   = typename time_point::duration;
        using rep        = typename time_point::rep;
        using period     = typename time_point::period;

        explicit timer(Scheduler sched) noexcept;

        [[nodiscard]] auto get_scheduler() const noexcept -> scheduler;

        template<typename Rep, typename Period>
        [[nodiscard]] auto async_wait(std::chrono::duration<Rep, Period> duration) const noexcept;
            // sender: set_value() | set_stopped()

        [[nodiscard]] auto async_wait_until(time_point deadline) const noexcept;
            // sender: set_value() | set_stopped()

        auto cancel() -> void;
    };
}
```

## API Reference

### timer (constructor)

```cpp
explicit timer(Scheduler sched) noexcept;
```

Binds the timer to a scheduler. The timer holds a copy of the scheduler; the underlying execution context must outlive all waits. Class template argument deduction works: `coio::timer timer{ctx.get_scheduler()};`.

### async_wait

```cpp
template<typename Rep, typename Period>
[[nodiscard]] auto async_wait(std::chrono::duration<Rep, Period> duration) const noexcept;
```

Returns a sender equivalent to `stop_when(sched.schedule_after(duration_cast<timer::duration>(duration)), timer-token)`.

- **Completes** with `set_value()` once the deadline is reached. The deadline is `now() + duration`, computed when `async_wait` is *called* — starting the returned sender later does not restart the clock.
- **Completes** with `set_stopped()` if the wait is cancelled — by this timer's `cancel()`, or by a stop request from the surrounding operation (both tokens are observed).
- The completion is delivered by the scheduler's execution context, on its consumer thread.
- The duration is converted to the scheduler's duration type with `std::chrono::duration_cast`.

### async_wait_until

```cpp
[[nodiscard]] auto async_wait_until(time_point deadline) const noexcept;
```

As `async_wait`, but completes at an absolute `deadline` (equivalent to `stop_when(sched.schedule_at(deadline), timer-token)`). A deadline in the past completes as soon as the context processes it.

### cancel

```cpp
auto cancel() -> void;
```

Requests stop on the timer's internal stop source. Every outstanding wait created from this timer receives the stop request and completes with `set_stopped()` (per the library cancellation contract, a wait that has already fired still delivers `set_value()`).

!!! warning "cancel() is sticky"
    The internal stop source is never reset. After `cancel()`, **all** waits subsequently created from this timer complete immediately with `set_stopped()`. Treat a cancelled timer as spent; construct a new `timer` for new waits.

### get_scheduler

```cpp
[[nodiscard]] auto get_scheduler() const noexcept -> scheduler;
```

Returns a copy of the bound scheduler.

## Thread safety

Creating and starting waits, and calling `cancel()`, are safe from any thread — the heavy lifting is done by the execution context's internal timer queue (a lock-protected intrusive timer heap accepting submission and cancellation from any thread) and by `inplace_stop_source`, both thread-safe. Completions are delivered on the context's consumer thread (see [Execution contexts](../execution/contexts.md)).

## Example

From `examples/hello.cpp` — two timed jobs plus a task that drives the loop:

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>

using namespace std::chrono_literals;

auto job(coio::time_loop::scheduler sched, std::string_view name,
         int value, std::chrono::seconds timeout) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(timeout);
    std::println("{} completed", name);
    co_return value;
}

auto main() -> int {
    coio::time_loop context;
    auto [i, j] = coio::this_thread::sync_wait(coio::when_all(
        job(context.get_scheduler(), "foo", 114, 2s),
        job(context.get_scheduler(), "bar", 514, 1s),
        [&context]() -> coio::task<> {
            context.run();
            co_return;
        }()
    )).value();
    std::println("result: i = {}, j = {}", i, j); // i = 114, j = 514, after ~2s
}
```

## See also

- [time_loop](../execution/time-loop.md) — `schedule_after` / `schedule_at` / `now`
- [Execution contexts](../execution/contexts.md) — where completions run
- [Waiting & Algorithms](algorithms.md) — `stop_when`, `when_any` (e.g. for timeouts)
- [Thread safety](../thread-safety.md)
