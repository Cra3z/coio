# task

`coio::task<T, Alloc, Sched>` is a lazily-started, move-only coroutine type that is also a sender. It is the workhorse of coio: write asynchronous logic as a coroutine body, then compose the result with `co_await`, sender algorithms, or `sync_wait`.

Header: `#include <coio/task.h>`

## Overview

Use `task` whenever you want to express asynchronous work as a coroutine. A `task`:

- is **lazy** — creating it does nothing; the body runs only when the task is awaited or its operation state is started;
- is **move-only** and single-shot — a task can be awaited/connected exactly once;
- is a **sender** — completion signatures are `set_value_t(T)` (or `set_value_t()` for `T = void`), `set_error_t(std::exception_ptr)`, `set_stopped_t()`;
- is **scheduler-affine** — every sender awaited inside the task resumes the coroutine on the task's scheduler;
- supports **allocator customization** of the coroutine frame, exposed to the body via `get_allocator`.

The default `task<T>` uses [`polymorphic_scheduler`](../execution/polymorphic-scheduler.md), so it can be started from any environment that provides a scheduler. `inline_task<T>` uses `execution::inline_scheduler` and performs no re-scheduling at all.

## Synopsis

```cpp
namespace coio {
    template<typename T = void, typename Alloc = void, typename Sched = polymorphic_scheduler>
    class task {
    public:
        using value_type = T;
        using allocator_type = Alloc;
        using scheduler_type = Sched;
        using promise_type = /*unspecified*/;
        using sender_concept = execution::sender_tag;
        using completion_signatures = execution::completion_signatures<
            execution::set_value_t(T),   // execution::set_value_t() if T is void
            execution::set_error_t(std::exception_ptr),
            execution::set_stopped_t()
        >;

        task() = default;
        task(const task&) = delete;
        task(task&& other) noexcept;
        ~task();

        auto operator= (const task&) -> task& = delete;
        auto operator= (task&& other) noexcept -> task&;

        explicit operator bool() const noexcept;

        auto affine() && noexcept -> task;

        template<stoppable_promise ReceiverPromise>
        auto as_awaitable(ReceiverPromise& receiver) && noexcept -> /*awaiter*/;

        template<execution::receiver Receiver>
        auto connect(Receiver receiver) && noexcept -> /*operation-state*/;

        auto swap(task& other) noexcept -> void;
        friend auto swap(task& lhs, task& rhs) noexcept -> void;
    };

    template<typename T = void, typename Alloc = void>
    using inline_task = task<T, Alloc, execution::inline_scheduler>;
}
```

## API Reference

### Template parameters

| Parameter | Constraint | Meaning |
|-----------|-----------|---------|
| `T` | `void`, a move-constructible unqualified object type, or an lvalue-reference type | the value produced by `co_return` |
| `Alloc` | `void` or an allocator whose `allocator_traits<Alloc>::pointer` is a raw pointer | coroutine-frame allocation policy (see below) |
| `Sched` | a scheduler whose `schedule()` cannot complete with an error | the scheduler the task is affine to |

Violating the constraints on `T` or `Alloc` is a compile-time error (`static_assert`). The constraint on `Sched` (`infallible_scheduler`) is checked when the task is connected or awaited; all coio context schedulers, `polymorphic_scheduler` and `inline_scheduler` satisfy it.

### Construction, assignment, state

```cpp
task() = default;
task(task&& other) noexcept;
auto operator= (task&& other) noexcept -> task&;
explicit operator bool() const noexcept;
auto swap(task& other) noexcept -> void;
```

A default-constructed or moved-from task is *empty*; `operator bool` returns `false` for it. Destroying a task that was never started destroys the suspended coroutine frame. Tasks are not copyable.

**Preconditions:** `connect` and `as_awaitable` (and therefore `co_await`) require a non-empty task and consume it — the task is empty afterwards. Awaiting or connecting an empty task is undefined behavior.

### Completion and error propagation

- `co_return v;` completes the task with `set_value(v)`; a `task<void>` completes with `set_value()`.
- An exception escaping the body completes the task with `set_error(std::exception_ptr)`. When the task is `co_await`ed, that exception is rethrown from the `co_await` expression in the awaiting coroutine.
- If an operation awaited inside the body completes with `set_stopped`, the body does not resume; the task itself completes with `set_stopped`, propagating outward through the awaiting coroutine's `unhandled_stopped` without unwinding via exceptions.

### Scheduler affinity

When a task is connected or awaited, its scheduler (`Sched`) is constructed from the **parent environment**: uses-allocator construction from `get_start_scheduler(env)` (falling back to `get_scheduler(env)`, then to `inline_scheduler`) with `get_allocator(env)` (defaulting to `std::allocator` when the environment provides none). Consequences:

- A `task<T>` (default `Sched = polymorphic_scheduler`) can be started from any environment with a scheduler — the parent's scheduler is type-erased.
- A context-bound task such as `epoll_context::task<T>` (`Sched = epoll_context::scheduler`) must be started from an environment whose scheduler is (convertible to) that concrete scheduler type — e.g. via `starts_on(ctx.get_scheduler(), ...)` or by being awaited from another task on the same context.

Every `co_await <sender>` inside the body is transformed so the sender is *affine* to the task's scheduler: if the sender has an `affine()` member it is used, otherwise the sender is wrapped in `execution::affine(...)`. Either way, **the coroutine always resumes on the task's scheduler** after an await, even if the awaited operation completed on another thread.

```cpp
auto affine() && noexcept -> task;
```

Returns the task unchanged. A task is already affine — its body always runs on its own scheduler, which is constructed from the awaiting environment — so awaiting a task from another task inserts no extra re-scheduling hop.

The task's environment answers `execution::get_scheduler` and `execution::get_start_scheduler` with the task's scheduler; read it in the body with `co_await coio::read_scheduler()`.

### Allocator support

The coroutine frame is allocated according to `Alloc` and the coroutine's arguments, following the standard **leading-allocator-argument convention**:

- If the coroutine's parameter list starts with `std::allocator_arg_t` followed by an allocator (for member functions: after the object parameter), that allocator allocates the frame.
    - With `Alloc = void` (the default), *any* allocator type may be passed; deallocation is type-erased inside the frame.
    - With a concrete `Alloc`, the passed allocator must be convertible to `Alloc`.
- Otherwise the frame is allocated with a default-constructed allocator: `std::allocator` for `Alloc = void`, else a default-constructed `Alloc` (which must be default-initializable).

The task's environment answers `get_allocator` with the frame's allocator:

- `Alloc = void`: presented as `std::pmr::polymorphic_allocator<>` (a passed pmr allocator is forwarded directly; any other allocator is wrapped in an internal `memory_resource`).
- concrete `Alloc`: presented as `Alloc` rebound to `std::byte`.

Read it with `co_await coio::read_allocator()` and hand it to pmr-aware containers so they share the frame's memory resource.

### Cancellation

The task's environment answers `get_stop_token` with an `inplace_stop_token` chained from the awaiting/connecting environment's token: a stop request upstream is visible to every operation awaited inside the task. See [Core Concepts — cancellation model](../concepts.md#cancellation-model) for the exact semantics (a stop request never overwrites a real result).

### Sender interface

```cpp
template<execution::receiver Receiver>
auto connect(Receiver receiver) && noexcept -> /*operation-state*/;

template<stoppable_promise ReceiverPromise>
auto as_awaitable(ReceiverPromise& receiver) && noexcept -> /*awaiter*/;
```

`connect` yields an operation state whose `start()` resumes the coroutine; `as_awaitable` is used automatically when the task is `co_await`ed inside another coroutine with awaitable-sender support and uses symmetric transfer (no stack growth for deep task chains). The `stoppable_promise` constraint means a `task` can only be `co_await`ed from a coroutine whose promise provides a noexcept `unhandled_stopped()` — coio's own coroutine types qualify; a plain hand-rolled coroutine promise does not. Both consume the task; both `static_assert` that `Sched` is an infallible scheduler for the receiver's environment.

**Thread safety:** a task object is not thread-safe; complete the transfer of a task between threads with proper synchronization. Once started, where the body runs is governed by its scheduler.

### inline_task

```cpp
template<typename T = void, typename Alloc = void>
using inline_task = task<T, Alloc, execution::inline_scheduler>;
```

A task with no scheduler affinity: awaited senders resume the coroutine wherever they complete. Cheaper (no re-scheduling, no type erasure), but the body may run on backend consumer threads — use it for glue code that is safe anywhere, e.g. small adapters like a signal watchdog.

### sync_wait interop

`coio::this_thread::sync_wait(std::move(t))` blocks the current thread until the task completes and returns `std::optional<std::tuple<T>>` (empty on `set_stopped`; rethrows on error). `sync_wait` drives an internal `run_loop` whose scheduler becomes the parent scheduler of the awaited task — so a default `task<>` works with `sync_wait` out of the box.

## Example

Allocator propagation and scheduler affinity (adapted from `examples/task.cpp`):

```cpp
#include <chrono>
#include <iostream>
#include <memory_resource>
#include <coio/core.h>
#include <coio/execution_context.h>

auto bar(std::allocator_arg_t, auto) -> coio::task<> {
    auto alloc = co_await coio::read_allocator();  // the frame's allocator, as pmr
    std::pmr::string str{"bar: allocated from the caller's buffer", alloc};
    std::cout << str << '\n';
}

auto foo(std::allocator_arg_t, auto) -> coio::task<> {
    std::cout << "foo\n";
    // forward the allocator: bar's frame comes from the same buffer
    co_await bar(std::allocator_arg, co_await coio::read_allocator());
}

auto baz() -> coio::task<void, void, coio::time_loop::scheduler> {
    using namespace std::chrono_literals;
    auto sched = co_await coio::read_start_scheduler();  // time_loop::scheduler
    co_await sched.schedule_after(1s);
    std::cout << "baz, one second later\n";
}

auto main() -> int {
    {
        std::byte buffer[1024];
        std::pmr::monotonic_buffer_resource resource{buffer, sizeof buffer};
        // foo's frame (and bar's) is allocated inside `buffer`
        coio::this_thread::sync_wait(
            foo(std::allocator_arg, std::pmr::polymorphic_allocator<>{&resource}));
    }
    {
        coio::time_loop loop;
        coio::async_scope scope;
        scope.spawn(coio::starts_on(loop.get_scheduler(), baz()));  // gives baz its scheduler
        loop.run();
        coio::this_thread::sync_wait(scope.join());
    }
}
```

## See also

- [Core Concepts](../concepts.md) — senders vs. coroutines, cancellation, environments
- [generator](generator.md) — synchronous lazy sequences
- [Execution contexts](../execution/contexts.md) — where task bodies actually run
- [polymorphic_scheduler](../execution/polymorphic-scheduler.md) — the default `Sched`
- [async_scope](../utils/async-scope.md) — spawning detached tasks
