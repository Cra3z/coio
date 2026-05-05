# coio

**coio** is a modern C++20 library providing asynchronous programming facilities built on **std::execution** (the [P2300](https://wg21.link/P2300) sender/receiver model) and C++20 coroutines.

## Features

- **Sender/Receiver model** — composable asynchronous algorithms via `std::execution`
- **Coroutine types** — `task<T, Alloc>` for async computations and `generator<Ref, Val>` for lazy sequences
- **Execution contexts** — `time_loop`, `epoll_context`, `uring_context`, `iocp_context` with thread-safe `run()`
- **Networking** — TCP/UDP sockets with sync and async operations (Linux & Windows)
- **Synchronization** — `async_mutex`, `async_semaphore`, `async_latch` that suspend coroutines instead of blocking threads
- **Utilities** — timers, concurrent queues, signal handling, stop tokens, and more

## Quick Example

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include <iostream>

auto job(coio::time_loop::scheduler sched, std::string_view name,
         std::chrono::seconds timeout) -> coio::task<> {
    coio::timer timer{sched};
    co_await timer.async_wait(timeout);
    std::cout << name << " completed\n";
}

int main() {
    coio::time_loop context;
    coio::async_scope scope;
    scope.spawn(job(context.get_scheduler(), "foo", std::chrono::seconds{2}));
    scope.spawn(job(context.get_scheduler(), "bar", std::chrono::seconds{1}));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## Platform Support

| Platform | Async I/O Backend | Status |
|----------|-------------------|--------|
| Linux    | epoll            | Stable |
| Linux    | io_uring         | Stable |
| Windows  | IOCP             | Stable |

## Core Concepts

coio is built around three key abstractions:

1. **Senders** (P2300) — composable, lazy descriptions of asynchronous work
2. **Coroutines** — `coio::task<T>` is both a coroutine and a sender, seamlessly bridging the two models
3. **Execution contexts** — event loops (`time_loop`, `epoll_context`, `uring_context`, `iocp_context`) that execute scheduled work

```cpp
// Sender composition
auto work = sched.schedule()
          | coio::then([] { return 42; })
          | coio::then([](int x) { return x * 2; });

// Coroutine style
auto work() -> coio::task<int> {
    co_await sched.schedule();
    co_return 42;
}
```
