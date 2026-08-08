# work_guard

`coio::work_guard` is an RAII handle that marks an execution context as having outstanding work, keeping its `run()` loop alive until the guard is released.

Header: `#include <coio/execution_context.h>`

## Overview

A context's `run()` returns as soon as its outstanding-work count reaches zero. That is exactly right for "run until everything finishes", but wrong for a worker thread that should keep serving work submitted later — between operations, the count would momentarily hit zero and `run()` would return early. A `work_guard` pins the count above zero for its lifetime: construct one before starting `run()` on a worker, destroy it to let `run()` drain and return.

`work_guard` is a thin, exception-safe wrapper over the context's [`work_started()`/`work_finished()`](contexts.md#work-tracking); prefer it over calling those manually.

## Synopsis

```cpp
namespace coio {
    template<typename ExecutionContext>
    concept execution_context = requires(ExecutionContext& context) {
        { context.get_scheduler() } -> execution::scheduler;
        context.work_started();
        context.work_finished();
    };

    template<execution_context ExecutionContext>
    class work_guard {
    public:
        work_guard() = default;
        explicit work_guard(ExecutionContext& context) noexcept;
        work_guard(const work_guard& other) noexcept;
        work_guard(work_guard&& other) noexcept;
        ~work_guard();

        auto operator= (work_guard other) noexcept -> work_guard&;
    };
}
```

## API Reference

### Constructors / destructor

```cpp
work_guard() = default;
```

An empty guard tracking no context; useful as a member that is armed later by assignment.

```cpp
explicit work_guard(ExecutionContext& context) noexcept;
```

Calls `context.work_started()`. The guard holds one unit of work until destroyed. The context must outlive the guard.

```cpp
work_guard(const work_guard& other) noexcept;   // copies also count: work_started() again
work_guard(work_guard&& other) noexcept;        // transfers ownership; source becomes empty
~work_guard();                                  // work_finished() if non-empty
```

Copying a guard registers an additional unit of work on the same context; moving transfers the existing unit. The destructor releases the guard's unit (if any) — when this drops the context's count to zero, `run()` wakes and returns.

```cpp
auto operator= (work_guard other) noexcept -> work_guard&;
```

Copy-and-swap: the left-hand guard takes over `other`'s state and releases its previous one.

**Thread safety:** guards may be created and destroyed from any thread (`work_started`/`work_finished` are thread-safe). A single guard object is not itself thread-safe.

!!! warning
    Every unit of work must be released before the context is destroyed — a context destructor with a nonzero work count calls `std::terminate()`. Destroy all guards (and let `run()` return) before the context goes out of scope.

## Typical patterns

**Worker thread that outlives its work.** Arm a guard before `run()` so the loop survives idle periods; drop the guard to initiate shutdown:

```cpp
coio::time_loop loop;
coio::work_guard<coio::time_loop> guard{loop};
std::jthread worker{[&] { loop.run(); }};

// ... submit work to loop.get_scheduler() from any thread ...

// shutdown: release the guard; run() returns once remaining work drains
guard = {};
```

**Pinning a context used only as a target.** When a context only receives `continues_on`/`schedule()` transfers, there may be moments with nothing queued; a guard keeps the consumer alive between transfers (see the worker example on the [contexts page](contexts.md#example)).

## Example

```cpp
#include <iostream>
#include <thread>
#include <coio/core.h>
#include <coio/execution_context.h>

auto main() -> int {
    coio::time_loop loop;

    std::jthread worker;
    {
        coio::work_guard<coio::time_loop> guard{loop};
        worker = std::jthread{[&] {
            const auto n = loop.run();               // stays alive while the guard exists
            std::cout << "processed " << n << " items\n";
        }};

        coio::this_thread::sync_wait(
            coio::schedule(loop.get_scheduler())
            | coio::then([] { std::cout << "ran on the worker\n"; }));
    }   // guard released here -> run() returns, worker joins

    // loop destroyed after run() returned and all work finished
}
```

## See also

- [Execution contexts](contexts.md) — work tracking, `run()` return conditions, destructor semantics
- [time_loop](time-loop.md)
