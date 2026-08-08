# async_scope

`coio::async_scope` is a container for "fire-and-forget" asynchronous work in the style of [P3149 `counting_scope`](https://wg21.link/p3149). It lets you eagerly start senders whose lifetime is not tied to a single awaiting coroutine, while still guaranteeing — via `join()` — that everything has finished before the scope (and the resources the work references) goes away.

Header: `#include <coio/utils/async_scope.h>` (also available via `<coio/core.h>`)

## Overview

`async_scope` is a thin wrapper over the underlying `std::execution` implementation's `counting_scope`. Each spawned sender is *associated* with the scope; `join()` returns a sender that completes once every association has been released. The scope supports cooperative mass-cancellation via `request_stop()` and can be sealed against new work via `close()`.

Use it when:

- You need to start work eagerly from non-async code (e.g. `main`, a callback) and wait for it later.
- Many independent operations share resources that must outlive them all.

## Synopsis

```cpp
namespace coio {
    class async_scope {
    public:
        using token = execution::counting_scope::token;

        static constexpr std::size_t max_associations = /* implementation limit */;

        async_scope();
        async_scope(const async_scope&) = delete;
        ~async_scope();

        [[nodiscard]] auto join() noexcept;              // sender: set_value()
        [[nodiscard]] auto get_token() noexcept -> token;

        auto request_stop() noexcept -> void;
        auto close() noexcept -> void;

        template<execution::sender Sndr, typename Env = execution::env<>>
        auto spawn(Sndr sndr, Env env = {}) noexcept -> void;
        auto spawn_on(execution::scheduler auto sched,
                      execution::sender auto sndr) noexcept -> void;

        template<execution::sender Sndr, typename Env = execution::env<>>
        [[nodiscard]] auto spawn_future(Sndr sndr, Env env = {}) noexcept;        // sender
        [[nodiscard]] auto spawn_future_on(execution::scheduler auto sched,
                                           execution::sender auto sndr) noexcept; // sender
    };
}
```

## API Reference

### spawn / spawn_on

```cpp
template<execution::sender Sndr, typename Env = execution::env<>>
auto spawn(Sndr sndr, Env env = {}) noexcept -> void;
auto spawn_on(execution::scheduler auto sched, execution::sender auto sndr) noexcept -> void;
```

Eagerly connects and starts `sndr`, associating the operation with the scope. `spawn_on` first transfers the start onto `sched` (via `starts_on`). The completion values are discarded.

- **Constraints**: the sender's value completion must be `set_value()` (no values) — e.g. a `coio::task<>`.
- **Errors terminate**: the spawned sender is wrapped so that an error completion calls `std::terminate()`. Handle errors *inside* the spawned work (e.g. `try`/`catch` in the task body, or `upon_error`).
- **Cancellation**: the spawned operation observes the scope's stop token; `request_stop()` requests its cancellation. A spawned operation completing with `set_stopped` is fine — it simply releases its association.
- **Allocation**: the operation state is heap-allocated using, in order of preference, the allocator from the `env` argument (`execution::prop{get_allocator, alloc}`), the allocator from the sender's environment, or `std::allocator`. `spawn_on`/`spawn_future_on` pass an env with an allocator obtained from the scheduler when it advertises one, so work spawned onto a coio context uses that context's memory resource.
- After `close()`, or once `join()` has completed, spawning fails to associate: the sender is discarded and **not started**.

### spawn_future / spawn_future_on

```cpp
template<execution::sender Sndr, typename Env = execution::env<>>
[[nodiscard]] auto spawn_future(Sndr sndr, Env env = {}) noexcept;        // sender
[[nodiscard]] auto spawn_future_on(execution::scheduler auto sched,
                                   execution::sender auto sndr) noexcept; // sender
```

Like `spawn`/`spawn_on`, but the work's eventual result can be retrieved: the returned sender completes with the spawned sender's completion once it is available. The work starts **eagerly**, before the returned sender is connected. As with `spawn`, error completions of the spawned sender call `std::terminate()`, so the returned sender only completes with `set_value(...)` or `set_stopped()`.

### join

```cpp
[[nodiscard]] auto join() noexcept; // sender: set_value()
```

Returns a sender that completes with `set_value()` after every association (spawned operation, `associate`d sender, outstanding token use) has been released. `join()` does **not** request cancellation — combine with `request_stop()` for "stop and drain".

**Contract**: the scope must be *joined* — the `join()` sender started and completed — before the `async_scope` is destroyed, unless the scope was never used. Destroying a scope with live associations, or one that was used but never joined, is a contract violation.

```cpp
coio::this_thread::sync_wait(scope.join()); // typical shutdown
```

### request_stop

```cpp
auto request_stop() noexcept -> void;
```

Requests stop on the scope's internal stop source. All outstanding associated operations observe the request through their environment's stop token, and operations spawned afterwards observe it immediately at start. Cancellation is cooperative: work that ignores its stop token runs to completion.

### close

```cpp
auto close() noexcept -> void;
```

Seals the scope: subsequent attempts to associate new work fail, so later `spawn` calls discard their sender without starting it (and `spawn_future` senders complete with `set_stopped`). Already-running work is unaffected.

### get_token

```cpp
[[nodiscard]] auto get_token() noexcept -> token;
```

Returns the underlying `counting_scope` token, for direct use with the low-level `execution::associate`, `execution::spawn`, and `execution::spawn_future` algorithms. Note that work associated through the raw token bypasses `async_scope`'s terminate-on-error wrapper.

### max_associations

`static constexpr std::size_t max_associations` — the implementation's upper bound on simultaneously live associations.

## Thread safety

`spawn`, `spawn_on`, `spawn_future`, `spawn_future_on`, `request_stop`, `close`, and `get_token` may be called concurrently from multiple threads, concurrently with spawned work completing. The `join()` sender may likewise be started while spawns are still occurring; work spawned successfully before join completes is awaited by it.

!!! note
    Spawned work runs wherever its sender runs — `async_scope` does not schedule anything by itself. Use `spawn_on`, or senders already bound to a scheduler, to control placement.

## Example

Spawning timed jobs on a `time_loop` and joining before exit (from `examples/scope.cpp`):

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>

auto job(coio::time_loop::scheduler sched, std::string_view name,
         std::chrono::seconds timeout) -> coio::task<> {
    coio::timer timer{sched};
    co_await timer.async_wait(timeout);
    std::println("{} completed", name);
}

auto main() -> int {
    using namespace std::chrono_literals;
    coio::time_loop context;
    coio::async_scope scope;
    scope.spawn(job(context.get_scheduler(), "foo", 2s));
    scope.spawn(job(context.get_scheduler(), "bar", 1s));
    scope.spawn(job(context.get_scheduler(), "qux", 3s));
    context.run();
    coio::this_thread::sync_wait(scope.join()); // wait for all sub-tasks
}
```

## See also

- [task](../coroutines/task.md) — the usual spawned payload
- [Execution contexts](../execution/contexts.md) — running the work
- [Waiting & Algorithms](algorithms.md) — `sync_wait` for joining from `main`
- [Thread safety](../thread-safety.md)
