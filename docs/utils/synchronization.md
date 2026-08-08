# Synchronization Primitives

Async counterparts of `std::mutex`, `std::counting_semaphore`, and `std::latch`. Where the standard primitives block a thread, these **suspend the awaiting coroutine** (or, more generally, defer the sender operation) and resume it when the primitive becomes available — so they are safe to use inside async work without stalling an execution context's consumer thread.

Header: `#include <coio/sync_primitives.h>`

## Overview

| Primitive | Async operation(s) | Non-blocking probe | Release |
|-----------|--------------------|--------------------|---------|
| `async_mutex` | `lock()`, `lock_guard()` | `try_lock()` | `unlock()` |
| `async_semaphore<Count, Max>` | `acquire()` | `try_acquire()` | `release()` |
| `async_binary_semaphore<Count>` | alias for `async_semaphore<Count, 1>` | | |
| `async_latch<Count>` | `wait()`, `arrive_and_wait(n)` | `try_wait()` | `count_down(n)` |

All primitives are non-copyable and safe to use concurrently from multiple threads (see [Thread safety](#thread-safety) below). `async_mutex` and `async_semaphore` serve waiters in FIFO order; `async_latch` completes all waiters together when the counter reaches zero.

## Synopsis

```cpp
namespace coio {
    template<typename Mutex>
    concept basic_async_lockable = requires (Mutex&& mtx) {
        { mtx.lock() } -> execution::sender;   // sender of set_value()
        { mtx.unlock() } -> std::same_as<void>;
    };

    template<typename Mutex>
    concept async_lockable = basic_async_lockable<Mutex> and requires (Mutex&& mtx) {
        { mtx.try_lock() } -> boolean_testable;
    };

    template<typename AsyncMutex>  // AsyncMutex models basic_async_lockable
    class async_unique_lock {
    public:
        using mutex_type = AsyncMutex;

        async_unique_lock();
        async_unique_lock(mutex_type& mtx, std::adopt_lock_t) noexcept;
        async_unique_lock(mutex_type& mtx, std::defer_lock_t) noexcept;
        async_unique_lock(mutex_type& mtx, std::try_to_lock_t)
            requires async_lockable<AsyncMutex>;
        async_unique_lock(async_unique_lock&&) noexcept;
        ~async_unique_lock();                            // unlocks if owned

        auto operator= (async_unique_lock) noexcept -> async_unique_lock&;
        auto swap(async_unique_lock&) noexcept -> void;

        auto lock();                                     // sender of set_value()
        [[nodiscard]] auto try_lock() -> bool requires async_lockable<AsyncMutex>;
        auto unlock() -> void;
        [[nodiscard]] auto mutex() noexcept -> mutex_type*;
        [[nodiscard]] auto owns_lock() const noexcept -> bool;
        explicit operator bool() const noexcept;
        [[nodiscard]] auto release() noexcept -> mutex_type*;
    };

    class async_mutex {
    public:
        async_mutex();
        [[nodiscard]] auto lock() noexcept;        // sender: set_value()
        [[nodiscard]] auto lock_guard() noexcept;  // sender: set_value(async_unique_lock<async_mutex>)
        [[nodiscard]] auto try_lock() noexcept -> bool;
        auto unlock() -> void;
    };

    template<std::integral CountType = /* see below */,
             CountType LeastMaxValue = std::numeric_limits<CountType>::max()>
    class async_semaphore {
    public:
        using count_type = CountType;

        explicit async_semaphore(count_type init) noexcept;
        [[nodiscard]] static constexpr auto max() noexcept -> count_type;
        [[nodiscard]] auto acquire() noexcept;     // sender: set_value() | set_stopped()
        [[nodiscard]] auto try_acquire() noexcept -> bool;
        auto release() noexcept -> void;
        [[nodiscard]] auto count() const noexcept -> count_type;
    };

    template<typename CountType = /* see below */>
    using async_binary_semaphore = async_semaphore<CountType, 1>;

    template<typename CountType = /* see below */>
    class async_latch {
    public:
        using count_type = CountType;

        explicit async_latch(count_type count) noexcept;
        [[nodiscard]] static constexpr auto max() noexcept -> count_type;
        [[nodiscard]] auto count() const noexcept -> count_type;
        [[nodiscard]] auto try_wait() const noexcept -> bool;
        auto count_down(count_type n = 1) noexcept -> count_type; // returns remaining
        [[nodiscard]] auto wait() noexcept;                  // sender: set_value()
        [[nodiscard]] auto arrive_and_wait(count_type n = 1) noexcept; // sender: set_value()
    };
}
```

The default `CountType` is `std::atomic_unsigned_lock_free::value_type`.

## API Reference

### async_mutex

An async mutual-exclusion lock. Lock-free bookkeeping; no OS blocking anywhere.

- `lock() -> sender` — completes with `set_value()` once the caller owns the mutex. If the mutex is free, the operation completes immediately (synchronously, on the caller's thread); otherwise the operation is queued and completed later by `unlock()`. The sender has **no** `set_stopped` completion: a pending `lock()` cannot be cancelled.
- `lock_guard() -> sender` — like `lock()`, but completes with `set_value(async_unique_lock<async_mutex>)`, an RAII guard adopting the freshly acquired lock.
- `try_lock() -> bool` — acquires without waiting; returns `true` on success.
- `unlock()` — releases the mutex and, if waiters are queued, completes the earliest one (FIFO). **Precondition**: the mutex is currently locked, and the caller is the owner (its `lock()`/`try_lock()` succeeded and it has not yet unlocked).

### async_unique_lock

`async_unique_lock<AsyncMutex>` is the async analogue of `std::unique_lock`, usable with any type modeling `basic_async_lockable` (e.g. `async_mutex`). Move-only. The destructor unlocks if the lock is owned.

- Construction: default (no mutex); `adopt_lock` (assumes ownership); `defer_lock` (no ownership); `try_to_lock` (calls `try_lock()`, requires `async_lockable`).
- `lock() -> sender` — acquires the associated mutex; on completion `owns_lock()` is `true`. **Precondition**: a mutex is associated and not currently owned by this guard.
- `try_lock()`, `unlock()`, `owns_lock()`, `operator bool`, `mutex()`, `release()`, `swap()` — as for `std::unique_lock`.

### async_semaphore

`async_semaphore<CountType, LeastMaxValue>` — an async counting semaphore.

- `async_semaphore(init)` — **precondition**: `0 <= init <= max()`.
- `acquire() -> sender` — completes with `set_value()` once a permit is obtained. If a permit is available at start, completes immediately on the caller's thread. Supports **cancellation**: if the operation's stop token is triggered while waiting, the waiter is removed and completes with `set_stopped()` (a stop request that races with a successful grant may still complete with `set_value()`).
- `try_acquire() -> bool` — obtains a permit without waiting.
- `release()` — if waiters are queued, grants the permit directly to the earliest waiter (FIFO) and completes it; otherwise increments the counter. Calling `release()` when the counter is already `max()` and no waiter is queued calls `std::terminate()`.
- `count()` — current number of available permits (a snapshot; may be stale immediately).
- `max()` — `LeastMaxValue`.

`async_binary_semaphore<CountType>` is `async_semaphore<CountType, 1>`.

### async_latch

`async_latch<CountType>` — a single-use async countdown latch.

- `async_latch(count)` — sets the initial counter.
- `count_down(n = 1) -> count_type` — atomically decrements by `n` and returns the remaining count; when the counter reaches zero, all queued waiters are completed. **Precondition**: the internal counter is at least `n`.
- `wait() -> sender` — completes with `set_value()` when the counter reaches zero (immediately if already zero). Equivalent to `arrive_and_wait(0)`. No cancellation: the sender completes with `set_value()` only.
- `arrive_and_wait(n = 1) -> sender` — atomically counts down by `n` and waits for zero.
- `try_wait() -> bool` — `true` if the counter is zero.
- `count()` — current counter value (snapshot).

Like `std::latch`, the counter cannot be reset or incremented; the latch is single-use.

## Thread safety

All operations on all primitives may be invoked concurrently from any threads.

!!! note "Where waiters resume"
    At the sender level, these primitives complete queued waiters on the thread that performs the release — `unlock()`, `release()`, or the `count_down()` call that reaches zero; an operation that can complete immediately (uncontended `lock()`, available permit, counter already zero) completes synchronously on the initiating thread. No completion scheduler is advertised.

    Inside a `coio::task` with an associated scheduler this is invisible: awaited senders are scheduler-affine, so execution automatically resumes on the task's scheduler after the `co_await`, regardless of which thread performed the release. Only without an associated scheduler (e.g. `inline_task`, or a raw sender under `sync_wait`) does the continuation run inline on the releasing thread — there, it runs *inside* the releaser's call to `unlock()`/`release()`/`count_down()`, so keep critical sections short and avoid re-entrant surprises.

## Example

Three workers count down a latch when their work is done; the main thread waits for all of them, then releases them for cleanup (from `examples/async_latch.cpp`):

```cpp
#include <coio/core.h>
#include <coio/sync_primitives.h>

struct Job { std::string name, product; };

auto work(Job& job, coio::async_latch<>& work_done,
          coio::async_latch<>& start_clean_up) -> coio::task<> {
    job.product = job.name + " worked";
    work_done.count_down();
    co_await start_clean_up.wait();
    job.product = job.name + " cleaned";
}

auto main() -> int {
    Job jobs[]{{"Annika"}, {"Buru"}, {"Chuck"}};
    coio::async_latch<> work_done{std::ranges::size(jobs)};
    coio::async_latch<> start_clean_up{1};

    coio::async_scope scope;
    for (auto& job : jobs) {
        scope.spawn(work(job, work_done, start_clean_up));
    }

    coio::this_thread::sync_wait(work_done.wait());   // all jobs worked
    start_clean_up.count_down();                      // let them clean up
    coio::this_thread::sync_wait(scope.join());       // all jobs finished
}
```

## See also

- [Thread safety](../thread-safety.md) — the consolidated model
- [async_scope](async-scope.md) — structured background work
- [fifo](buffers.md#fifo) — an async MPMC channel built on `async_semaphore`
- [Waiting & Algorithms](algorithms.md) — `sync_wait`, `stop_when`
