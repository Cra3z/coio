# Core Concepts

coio is a C++20 asynchronous I/O library built on the sender/receiver model of `std::execution` (P2300). This page explains the ideas that every other page builds on: how coroutines and senders compose, how cancellation works, where completions are delivered, and what queries an operation's environment answers.

Header: `#include <coio/core.h>`

## Overview

coio does not choose between coroutines and senders — it treats them as two views of the same thing:

- `coio::task` is a coroutine type **and** a sender. You can `co_await` it, `connect` it to a receiver, pass it to `std::execution` algorithms (`when_all`, `then`, ...), or hand it to `coio::this_thread::sync_wait`.
- Inside a `coio::task`, you can `co_await` **any sender**. The task's promise transforms awaited senders into awaitables, so context schedule operations, timers, socket I/O and user-defined senders all compose with plain `co_await`.

The `std::execution` implementation is pluggable: coio re-exports it under `coio::execution` from NVIDIA stdexec, beman.execution, or a C++26 standard library, selected at configure time. `<coio/core.h>` also re-exports the most common algorithms (`coio::then`, `coio::when_all`, `coio::continues_on`, ...) so typical code does not need to spell out the backing namespace.

## Synopsis

```cpp
namespace coio {
    // pluggable std::execution backend (stdexec / beman / C++26 <execution>)
    namespace execution { /* re-exported P2300 facilities */ }

    // common algorithms re-exported at namespace scope:
    // just, then, let_value, when_all, schedule, continues_on, starts_on, on, ...

    namespace this_thread {
        // block until a sender completes
        auto sync_wait(auto&& sender);              // -> std::optional<std::tuple<Values...>>
        auto sync_wait_with_variant(auto&& sender);
    }

    // environment-reading senders: co_await one inside a task
    inline constexpr auto read_scheduler;        // -> execution::get_scheduler(env)
    inline constexpr auto read_start_scheduler;  // -> execution::get_start_scheduler(env)
    inline constexpr auto read_allocator;        // -> get_allocator(env)
    inline constexpr auto read_stop_token;       // -> get_stop_token(env)

    // attach an extra stop token to a sender  (header: <coio/utils/stop_token.h>)
    inline constexpr /*unspecified*/ stop_when;  // stop_when(sender, stop_token) -> sender

    // a type usable as an execution context     (header: <coio/execution_context.h>)
    template<typename ExecutionContext>
    concept execution_context = requires(ExecutionContext& context) {
        { context.get_scheduler() } -> execution::scheduler;
        context.work_started();
        context.work_finished();
    };
}
```

## API Reference

### Coroutines are senders, senders are awaitables

`coio::task<T, Alloc, Sched>` models `execution::sender` with completion signatures `set_value_t(T)` (or `set_value_t()` for `T = void`), `set_error_t(std::exception_ptr)`, and `set_stopped_t()`. Starting the task — by `connect`/`start`, by `co_await`ing it, or via `sync_wait` — resumes the lazily suspended coroutine.

In the other direction, the task promise's `await_transform` accepts any sender and adapts it with `execution::as_awaitable`. Before adapting, the sender is made **affine** to the task's scheduler: after the `co_await` completes, the coroutine is guaranteed to resume on the task's scheduler, even if the awaited operation completed elsewhere. Senders that are already affine (such as another `task`) expose an `affine()` member and skip the extra hop. See [task](coroutines/task.md) for the full model.

Error channels map naturally: a `set_error` completion of an awaited sender is rethrown as an exception from the `co_await` expression; an exception escaping a task body becomes a `set_error(std::exception_ptr)` completion of the task-as-sender.

### Cancellation model

Cancellation in coio is cooperative and flows through **stop tokens**:

- Every operation receives a stop token from its receiver's environment (`get_stop_token`). Inside a task, the promise chains the awaiting environment's token into its own `inplace_stop_token`, so a stop request made anywhere up the chain reaches the innermost pending operation.
- `coio::stop_when(sender, stop_token)` attaches an *additional* stop token to a sender: the resulting sender observes a stop request when either the outer environment's token or the given token is triggered. Execution contexts use this internally to tie timers and I/O operations to the context's own stop source (`request_stop()`).
- When a `co_await`ed operation completes with `set_stopped`, the awaiting coroutine body does **not** resume; the stopped completion propagates outward (through `unhandled_stopped`) until some algorithm handles it, ultimately completing the outermost task with `set_stopped`.

The semantics of a stop request are precise:

- A stop request **asks** the backend to cancel; it does not overwrite an actual target-operation result.
- The receiver gets `set_stopped()` only when the cancellation path wins, for example when a pending operation is synchronously removed or the target backend reports cancellation. A target success or ordinary error remains `set_value()` or `set_error()` even if stop was requested first.
- A stop request that already exists at `start()` does not skip backend initiation. An immediate target result wins; if the operation remains pending, coio then asks the backend to cancel it.

!!! note
    Cancellation is a *race you are allowed to lose*: after requesting stop you may still observe a value or error completion. Write completion handling accordingly.

### Completion-delivery guarantee

Work submitted to an execution context is completed by the context's active `run()`/`poll()` consumer thread. This holds for **all three completion channels** — `set_value`, `set_error` and `set_stopped` (including synchronous initiation failures and cancellation): every completion is delivered through the context's operation queue, and the initiating thread is never called back inline.

Context senders advertise this via `get_completion_scheduler` for all three CPOs (`set_value_t`, `set_error_t`, `set_stopped_t`), so algorithms like `continues_on` can elide a re-schedule when already targeting the same scheduler.

See [Execution contexts](execution/contexts.md) for the MPSC thread-safety model behind this guarantee.

### Environments

Receivers carry an *environment* — a queryable bag of properties. coio operations use these queries:

| Query | Meaning |
|-------|---------|
| `execution::get_scheduler` | the scheduler associated with the current computation |
| `execution::get_start_scheduler` | the scheduler on which the operation is started; used to construct a child task's scheduler |
| `get_allocator` | the allocator for internal allocations (child operation state and coroutine environments) |
| `get_stop_token` | the stop token to observe for cancellation |

A task's promise answers all four: `get_scheduler`/`get_start_scheduler` return the task's own scheduler, `get_stop_token` returns the task's chained `inplace_stop_token`, and `get_allocator` forwards the allocator from the receiver environment after conversion to the task's allocator type. If the receiver environment has no allocator, a default-constructed task allocator is used. This environment allocator is independent of the coroutine frame allocator selected by `Alloc`.

Inside a task, read the environment by awaiting the helpers from `<coio/core.h>`:

```cpp
auto sched = co_await coio::read_scheduler();
auto alloc = co_await coio::read_allocator();
auto token = co_await coio::read_stop_token();
```

Context schedulers and context senders also answer `get_allocator` (the context's `std::pmr::memory_resource`) and `get_completion_scheduler` (the context's scheduler, for all three CPOs).

## Example

A task that inspects its environment, schedules onto a context, and is cancelled externally:

```cpp
#include <chrono>
#include <iostream>
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/stop_token.h>

using namespace std::chrono_literals;

auto ticker() -> coio::time_loop::task<> {
    auto sched = co_await coio::read_scheduler();   // the time_loop's scheduler
    for (int i = 0;; ++i) {
        co_await sched.schedule_after(100ms);       // cancellable timed wait
        std::cout << "tick " << i << '\n';
    }
}

auto stopper(coio::inplace_stop_source& stop) -> coio::time_loop::task<> {
    auto sched = co_await coio::read_scheduler();
    co_await sched.schedule_after(1s);
    stop.request_stop();                            // ticker completes with set_stopped
}

auto main() -> int {
    coio::time_loop loop;
    coio::inplace_stop_source stop;

    coio::async_scope scope;
    scope.spawn(coio::starts_on(
        loop.get_scheduler(),
        coio::stop_when(ticker(), stop.get_token())  // stop the infinite loop from outside
    ));
    scope.spawn(coio::starts_on(loop.get_scheduler(), stopper(stop)));

    loop.run();
    coio::this_thread::sync_wait(scope.join());
}
```

The stop request cancels the pending `schedule_after`; the `ticker` task completes with `set_stopped` on the loop's consumer thread, and `run()` returns once no work remains.

## See also

- [task](coroutines/task.md) — the coroutine/sender hybrid in detail
- [generator](coroutines/generator.md) — synchronous lazy sequences
- [Execution contexts](execution/contexts.md) — MPSC model, `run`/`poll`, work tracking
- [Algorithms](utils/algorithms.md) — `when_any`, `stop_when` and friends
- [Error handling](error-handling.md)
- [Thread safety](thread-safety.md)
