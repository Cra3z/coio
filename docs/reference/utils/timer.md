# coio::timer

**Header:** `<coio/utils/timer.h>`

An async timer bound to a scheduler. Timers support cooperative cancellation via `stop_when`.

## Template Parameters

| Parameter | Description |
|-----------|-------------|
| `Scheduler` | Must model `timed_scheduler`. |

## Member Types

| Type | Definition |
|------|-----------|
| `scheduler` | `Scheduler` |
| `time_point` | `decltype(Scheduler::now())` |
| `clock` | `time_point::clock` |
| `duration` | `time_point::duration` |
| `rep` | `time_point::rep` |
| `period` | `time_point::period` |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `explicit timer(Scheduler sched) noexcept` | Constructs a timer bound to the given scheduler. |

### Observers

| Name | Description |
|------|-------------|
| `get_scheduler() const noexcept -> scheduler` | Returns the associated scheduler. |

### Operations

| Name | Description |
|------|-------------|
| `async_wait(duration) const noexcept` | Returns a sender that completes after the given duration. Cancellable. |
| `async_wait_until(time_point) const noexcept` | Returns a sender that completes at the given time point. Cancellable. |
| `cancel() -> void` | Cancels any pending async wait. The sender completes with `set_stopped()`. |

### Completion

The sender returned by `async_wait` / `async_wait_until`:

| Channel | Trigger |
|---------|---------|
| `set_value()` | Timer elapsed normally. |
| `set_stopped()` | `cancel()` was called. |

## Example

```cpp
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include <coio/core.h>

auto timeout_job(coio::time_loop::scheduler sched) -> coio::task<> {
    coio::timer timer{sched};

    // Wait for 500ms
    co_await timer.async_wait(std::chrono::milliseconds{500});
    std::println("500ms elapsed");

    // Wait until a specific deadline
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    co_await timer.async_wait_until(deadline);
    std::println("Deadline reached");
}

// With cancellation
auto cancellable_wait(auto sched) -> coio::task<> {
    coio::timer timer{sched};

    coio::async_scope scope;
    scope.spawn([&timer]() -> coio::task<> {
        co_await timer.async_wait(std::chrono::seconds{10});
        std::println("This should not print");
    }());

    co_await coio::timer{sched}.async_wait(std::chrono::seconds{1});
    timer.cancel(); // cancel the 10-second wait
    co_await scope.join();
}
```

## Notes

- The timer uses the scheduler's `schedule_after`/`schedule_at` internally.
- Cancellation is implemented via `stop_when`: `async_wait` = `stop_when(schedule_after(dur), stop_source.get_token())`.
