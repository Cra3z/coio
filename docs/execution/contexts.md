# Execution Contexts

An execution context owns a queue of pending operations and a consumer loop that completes them. All coio contexts — [`time_loop`](time-loop.md), [`epoll_context`](epoll.md), [`uring_context`](uring.md) and [`iocp_context`](iocp.md) — share one model, one threading contract, and one public driving interface, described here.

Header: `#include <coio/execution_context.h>`

## Overview

A context gives you:

- a **scheduler** (`get_scheduler()`) — a cheap, copyable handle used to submit work: immediate transfers (`schedule()`), timed waits (`schedule_at`/`schedule_after`), and — for the I/O contexts — I/O objects;
- a **driving interface** (`run`/`run_one`/`poll`/`poll_one`) — you supply the consumer thread; the context never spawns threads of its own;
- **work tracking** (`work_started`/`work_finished`, [`work_guard`](work-guard.md)) — `run()` returns when the outstanding-work count reaches zero;
- a **stop source** (`request_stop()`) — cancels the context's outstanding timed waits and I/O operations so `run()` can drain and return.

Choose `time_loop` for portable, timer-only scheduling; choose an I/O context for sockets, files and pipes on the corresponding platform.

## Thread-safety model

All execution contexts are **MPSC** (multi-producer, single-consumer):

- **Multi-producer**: any thread may concurrently start operations on a context (`schedule()`, `schedule_at()`/`schedule_after()`, and the `async_*` operations of its I/O objects), and `get_scheduler()`, `work_started()`/`work_finished()` and `request_stop()` are thread-safe.
- **Single-consumer**: at most one thread may be inside `run()`, `run_one()`, `poll()` or `poll_one()` for a context at a time. This is a precondition and is not checked at runtime: concurrent consumer calls are undefined behavior. The consumer thread may change over the context's lifetime (e.g. `poll()` from one thread, later `run()` from another), provided the earlier call happens-before the later one (thread join, mutex, or similar synchronization).

### The three-channel completion invariant

Work submitted to the context is completed by its active `run()`/`poll()` consumer thread. This holds for **all three completion channels** — `set_value`, `set_error` and `set_stopped` (including synchronous initiation failures and cancellation): every completion is delivered through the context's operation queue, and the initiating thread is never called back inline. Context senders advertise this via `get_completion_scheduler` for all three CPOs, letting the library's scheduler-affinity machinery skip a redundant re-schedule when execution is already on the right scheduler. Any new operation added to a context must preserve this invariant (complete by posting to the queue, never inline).

## Synopsis

The interface common to every context (shown here as exposition; each context is a distinct concrete class):

```cpp
namespace coio {
    template<typename ExecutionContext>
    concept execution_context = requires(ExecutionContext& context) {
        { context.get_scheduler() } -> execution::scheduler;
        context.work_started();
        context.work_finished();
    };

    class /*execution-context*/ {          // time_loop, epoll_context, uring_context, iocp_context
    public:
        class scheduler;                   // see below

        template<typename T = void, typename Alloc = std::allocator<std::byte>>
        using task = coio::task<T, Alloc, scheduler>;

        /*context*/(const /*context*/&) = delete;
        ~/*context*/();                    // std::terminate() if work is still outstanding

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

    class /*execution-context*/::scheduler {
    public:
        using scheduler_concept = /*scheduler tag*/;

        [[nodiscard]] static auto now() noexcept -> std::chrono::steady_clock::time_point;

        [[nodiscard]] auto schedule() const noexcept -> /*sender of ()*/;
        [[nodiscard]] auto schedule_at(std::chrono::steady_clock::time_point deadline) const noexcept
            -> /*sender of ()*/;
        template<typename Rep, typename Period>
        [[nodiscard]] auto schedule_after(std::chrono::duration<Rep, Period> duration) const noexcept
            -> /*sender of ()*/;

        [[nodiscard]] auto context() const noexcept -> /*execution-context*/&;

        [[nodiscard]] static constexpr auto query(execution::get_forward_progress_guarantee_t) noexcept
            -> execution::forward_progress_guarantee;   // parallel
        [[nodiscard]] auto query(get_allocator_t) const noexcept -> std::pmr::polymorphic_allocator<>;

        friend auto operator== (const scheduler&, const scheduler&) -> bool;
    };
}
```

## API Reference

### Concept `execution_context`

```cpp
template<typename ExecutionContext>
concept execution_context = requires(ExecutionContext& context) {
    { context.get_scheduler() } -> execution::scheduler;
    context.work_started();
    context.work_finished();
};
```

The minimal interface required by [`work_guard`](work-guard.md) and other generic components. All four coio contexts model it.

### Driving the context

```cpp
auto run() -> std::size_t;
auto run_one() -> bool;
auto poll() -> std::size_t;
auto poll_one() -> bool;
```

The calling thread becomes the context's consumer for the duration of the call (single-consumer rule above).

| Function | Blocks? | Processes | Returns |
|----------|---------|-----------|---------|
| `run()` | yes | completions until the outstanding-work count reaches zero | number of completions processed |
| `run_one()` | yes | at most one completion | `true` if one was processed; `false` if no work was outstanding |
| `poll()` | no | all completions that are ready now | number of completions processed |
| `poll_one()` | no | at most one ready completion | `true` if one was processed |

`run()` and `run_one()` sleep (on the platform demultiplexer or a timer-aware wait) while work is outstanding but nothing is ready. All four return immediately when the work count is zero — use a [`work_guard`](work-guard.md) to keep `run()` from returning while producers may still submit work.

**Throws:** backend failures surface as `std::system_error` from the driving call.

### `request_stop`

```cpp
auto request_stop() -> void;
```

Requests stop on the context's internal stop source and wakes the consumer. Every timed wait (`schedule_at`/`schedule_after`) and every I/O operation initiated through the context is linked to this source via `stop_when`, so outstanding operations are cancelled and complete with `set_stopped` (subject to the [cancellation semantics](../concepts.md#cancellation-model): results that already exist are not overwritten). As the cancelled operations drain, the work count falls and `run()` returns. Thread-safe; idempotent.

!!! note
    Plain `schedule()` items are not tied to the context's stop source; they still complete normally (value, or stopped if *their own* receiver's stop token was triggered).

### Work tracking

```cpp
auto work_started() noexcept -> void;
auto work_finished() noexcept -> void;
```

Increment / decrement the outstanding-work count. Every operation started on the context calls these automatically; call them manually (or use [`work_guard`](work-guard.md)) to keep `run()` alive across gaps where no operation is yet pending. Thread-safe. Each `work_started()` must be balanced by exactly one `work_finished()`; when the count reaches zero the consumer is woken and `run()` returns.

### `get_scheduler` / `get_allocator`

```cpp
[[nodiscard]] auto get_scheduler() noexcept -> scheduler;
[[nodiscard]] auto get_allocator() const noexcept -> std::pmr::polymorphic_allocator<>;
```

`get_scheduler()` returns a scheduler handle for this context; thread-safe, and valid only while the context is alive. `get_allocator()` returns an allocator over the `std::pmr::memory_resource` the context was constructed with (every context accepts the resource at construction, defaulting to `std::pmr::get_default_resource()`); the context uses it for internal per-operation allocations, and context environments expose it via the `get_allocator` query.

### Destructor

Destroying a context whose outstanding-work count is not zero calls **`std::terminate()`**, mirroring `std::execution::run_loop`. Ensure all operations have completed — e.g. `run()` has returned and every `work_guard` is destroyed — before the context goes out of scope. The context must also outlive its schedulers, senders, and I/O objects.

### Scheduler operations

Schedulers are cheap, copyable, equality-comparable handles. All operations are thread-safe (multi-producer rule).

```cpp
[[nodiscard]] auto schedule() const noexcept -> /*sender of ()*/;
```

A sender that completes with `set_value()` on the context's consumer thread. If the receiver's stop token is stoppable, the completion signatures also include `set_stopped_t()`, and a stop request observed before the item is consumed completes it with `set_stopped()` instead.

```cpp
[[nodiscard]] auto schedule_at(std::chrono::steady_clock::time_point deadline) const noexcept;
template<typename Rep, typename Period>
[[nodiscard]] auto schedule_after(std::chrono::duration<Rep, Period> duration) const noexcept;
```

Senders that complete with `set_value()` on the consumer thread once `deadline` (respectively `now() + duration`) is reached, or with `set_stopped()` if cancelled first — by the receiver's stop token or by the context's `request_stop()`. Completion signatures: `set_value_t()`, `set_stopped_t()`.

```cpp
[[nodiscard]] static auto now() noexcept -> std::chrono::steady_clock::time_point;
```

The clock used by the timed operations (`std::chrono::steady_clock`).

```cpp
[[nodiscard]] auto context() const noexcept -> Context&;
```

The owning context.

**Queries:** `get_forward_progress_guarantee` → `forward_progress_guarantee::parallel`; `get_allocator` → the context's allocator.

**Equality:** two schedulers compare equal if and only if they refer to the same context object. The I/O context schedulers additionally satisfy coio's `io_scheduler` concept and add `make_io_object` (see the per-backend pages).

### Context sender environments

Every sender obtained from a context scheduler (schedule, timed, and I/O senders) has an environment that answers:

- `get_completion_scheduler<set_value_t>`, `get_completion_scheduler<set_error_t>`, `get_completion_scheduler<set_stopped_t>` — all return the context's scheduler (the three-channel invariant above);
- `get_allocator` — the context's allocator.

## Example

One `time_loop` per worker thread, kept alive with `work_guard`, targeted from outside via `continues_on` (adapted from `examples/schedule.cpp`):

```cpp
#include <iostream>
#include <thread>
#include <coio/core.h>
#include <coio/execution_context.h>

class worker {
public:
    worker() {
        thrd_ = std::jthread{[this] { loop_.run(); }};  // guard_ already holds work
    }

    worker(const worker&) = delete;

    auto scheduler() { return loop_.get_scheduler(); }

private:
    coio::time_loop loop_;
    std::jthread thrd_;
    coio::work_guard<coio::time_loop> guard_{loop_};  // destroyed first: releases run()
};

auto main() -> int {
    worker alice, bob;
    coio::this_thread::sync_wait(
        coio::just()
        | coio::then([] { std::cout << "main thread\n"; })
        | coio::continues_on(alice.scheduler())
        | coio::then([] { std::cout << "on alice's thread\n"; })
        | coio::continues_on(bob.scheduler())
        | coio::then([] { std::cout << "on bob's thread\n"; }));
}
```

Each `continues_on` submits a `schedule()` item from a foreign thread (multi-producer); the continuation runs on that worker's consumer thread. When each `worker` is destroyed, its `work_guard` releases the work count, `run()` returns, and the `jthread` joins before the `time_loop` is destroyed.

## See also

- [time_loop](time-loop.md) — the portable timer-driven context
- [epoll_context](epoll.md) / [uring_context](uring.md) / [iocp_context](iocp.md) — the I/O backends
- [work_guard](work-guard.md) — RAII work tracking
- [Core Concepts](../concepts.md) — cancellation and completion-delivery guarantees
- [Thread safety](../thread-safety.md)
