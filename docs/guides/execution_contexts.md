# Execution Contexts Guide

Execution contexts are the event loops that drive coio programs. They schedule tasks, manage timers, and handle I/O completions.

## Available Contexts

| Context | Platform | I/O Support | Best For |
|---------|----------|-------------|----------|
| `time_loop` | Cross-platform | None (timers only) | Timers, testing, CPU-bound work |
| `epoll_context` | Linux | epoll | Network servers |
| `uring_context` | Linux | io_uring | File I/O, high-throughput network |
| `iocp_context` | Windows | IOCP | Network and file I/O |

## Basic Pattern

Every coio program follows this pattern:

```cpp
#include <coio/execution_context.h>

int main() {
    // 1. Create a context
    coio::time_loop context;

    // 2. Get the scheduler
    auto sched = context.get_scheduler();

    // 3. Spawn work
    coio::async_scope scope;
    scope.spawn(
        sched.schedule()
        | coio::then([] { std::println("Hello from the event loop!"); })
    );

    // 4. Run the event loop
    context.run();

    // 5. Wait for completion
    coio::this_thread::sync_wait(scope.join());
}
```

## Choosing a Context

### `time_loop` — Timers and CPU work

Use when you don't need I/O:

```cpp
coio::time_loop ctx;
coio::timer timer{ctx.get_scheduler()};
co_await timer.async_wait(std::chrono::seconds{1});
```

### `epoll_context` — Linux network servers

The simplest I/O context on Linux:

```cpp
coio::epoll_context ctx;
auto sched = ctx.get_scheduler();
coio::tcp::acceptor acceptor{sched,
    coio::endpoint{coio::ipv4_address::any(), 8080}};
```

### `uring_context` — Linux file I/O and high-throughput

Required for async file operations:

```cpp
coio::uring_context ctx{256}; // 256 ring entries
auto sched = ctx.get_scheduler();
coio::stream_file file{sched, "/path/to/file",
    coio::stream_file::read_only};
```

### `iocp_context` — Windows

The only I/O context on Windows:

```cpp
coio::iocp_context ctx;
auto sched = ctx.get_scheduler();
```

## Event Loop Operations

Each context provides these event loop methods:

```cpp
ctx.run();       // Block until no work remains or stopped
ctx.run_one();   // Block until one item processed
ctx.poll();      // Process all ready items without blocking
ctx.poll_one();  // Process at most one item without blocking
```

All four methods are **thread-safe** — multiple threads can call them concurrently.

## Multi-threading

Run the event loop on multiple threads for parallelism:

```cpp
coio::time_loop ctx;

// Spawn compute-heavy tasks
for (int i = 0; i < 10; ++i) {
    scope.spawn(
        ctx.get_scheduler().schedule()
        | coio::then([i] {
            std::this_thread::sleep_for(1s);
            std::println("Task {} done", i);
        })
    );
}

// Run on multiple threads
std::vector<std::jthread> workers;
for (int i = 0; i < 4; ++i) {
    workers.emplace_back([&ctx] { ctx.run(); });
}

coio::this_thread::sync_wait(scope.join());
```

### Performance Consideration

When using multiple threads with I/O contexts:
- Work is dispatched to **any** thread calling `run()`
- This provides parallelism automatically
- No explicit thread pool or strand is needed for most use cases

## Work Guard

`work_guard` keeps the event loop alive when there is no scheduled work:

```cpp
coio::time_loop ctx;
{
    coio::work_guard guard{ctx}; // ctx.run() won't return yet
    // ... start operations that will eventually finish
}
// guard's destructor lets ctx.run() return when idle
ctx.run();
```

This is useful when the event loop must outlive the spawning scope.

## Requesting Stop

Call `request_stop()` to gracefully shut down:

```cpp
// Signal handler:
coio::signal_set signals{SIGINT, SIGTERM};
int sig = co_await signals.async_wait();
ctx.request_stop(); // drain remaining work, then return from run()
```

## Platform Portability

Write portable code by aliasing the context:

```cpp
#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif
```

Or use `uring_context` when you need file I/O on Linux:

```cpp
#if COIO_OS_LINUX
#include <coio/asyncio/uring_context.h>
using io_context = coio::uring_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif
```
