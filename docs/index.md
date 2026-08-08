# coio

A C++ **asynchronous I/O** library built on the [sender/receiver model](https://wg21.link/P2300) (`std::execution`), with first-class coroutine support.

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>

using namespace std::chrono_literals;

auto job(coio::time_loop::scheduler sched, int value, std::chrono::seconds delay) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(delay);
    co_return value;
}

auto main() -> int {
    coio::time_loop context;
    auto [i, j] = coio::this_thread::sync_wait(coio::when_all(
        job(context.get_scheduler(), 114, 2s),
        job(context.get_scheduler(), 514, 1s),
        [&context]() -> coio::task<> { context.run(); co_return; }()
    )).value();
    // i == 114, j == 514, total wall time ≈ 2s
}
```

## Features

- **Sender/receiver model** — every asynchronous operation is a sender, composable with `std::execution` algorithms and directly `co_await`-able inside coio coroutines.
- **Coroutine types** — [`task<T, Allocator, Scheduler>`](coroutines/task.md) for asynchronous computations, [`generator<Ref, Val, Allocator>`](coroutines/generator.md) for lazy synchronous sequences.
- **Execution contexts** — the portable [`time_loop`](execution/time-loop.md), plus native async I/O backends: [`epoll_context`](execution/epoll.md) and [`uring_context`](execution/uring.md) on Linux, [`iocp_context`](execution/iocp.md) on Windows.
- **Networking** — [TCP/UDP sockets](net/sockets.md) with synchronous and asynchronous operations, [address types](net/addresses.md) and a [resolver](net/resolver.md).
- **Files and pipes** — [stream and random-access files](io/files.md), [pipes](io/pipes.md), and [complete-transfer read/write algorithms](io/algorithms.md).
- **Synchronization** — [`async_mutex`, `async_semaphore`, `async_latch`](utils/synchronization.md): primitives that suspend coroutines instead of blocking threads.
- **Utilities** — [timers](utils/timer.md), [structured concurrency scopes](utils/async-scope.md), [signal handling](utils/signal-wait.md), [buffers and concurrent queues](utils/buffers.md).

!!! note
    Async I/O and networking backends are currently implemented on Linux (epoll and io_uring; the io_uring backend requires kernel 5.19 or newer) and Windows (IOCP).

## Where to start

| Goal | Page |
|------|------|
| Build and install the library | [Getting Started](getting-started.md) |
| Understand senders, coroutines, and cancellation in coio | [Concepts](concepts.md) |
| Write your first async program | [task](coroutines/task.md), [time_loop](execution/time-loop.md) |
| Do network I/O | [Sockets](net/sockets.md), [I/O Model & Lifetime](io/model.md) |
| Understand the threading contracts | [Execution Contexts](execution/contexts.md), [Thread Safety Summary](thread-safety.md) |

## License

coio is distributed under the [MIT License](https://opensource.org/licenses/MIT).
