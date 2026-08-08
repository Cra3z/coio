# Getting Started

This page walks through building coio, integrating it into a CMake project, and writing a first program.

## Requirements

- A **C++20**/**C++23** compatible compiler (recent GCC, Clang, or MSVC).
- **CMake 3.26+**.
- Platform backends:
    - Linux: `epoll_context` works on any modern kernel; `uring_context` additionally needs [liburing](https://github.com/axboe/liburing) and a **5.19+ kernel at runtime** (probed at context construction).
    - Windows: `iocp_context` uses I/O Completion Ports.
- A `std::execution` implementation (fetched automatically depending on the selected backend, see below).

## Building

```bash
cmake -S . -B build
cmake --build build
```

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `COIO_BUILD_EXAMPLES` | `OFF` | Build the example programs under `examples/` |
| `COIO_BUILD_TESTS` | `OFF` | Build the [doctest](https://github.com/doctest/doctest)-based tests |
| `COIO_BUILD_WITH_ASAN` | `OFF` | Enable AddressSanitizer |
| `COIO_BUILD_WITH_TSAN` | `OFF` | Enable ThreadSanitizer |
| `COIO_BUILD_WITH_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |
| `COIO_SENDERS_BACKEND` | `NVIDIA` | Which `std::execution` implementation to use (see below) |

### Choosing a `std::execution` backend

coio programs against standard `std::execution`; the concrete implementation is selected at configure time with `COIO_SENDERS_BACKEND`:

| Value | Implementation |
|-------|----------------|
| `NVIDIA` | [NVIDIA/stdexec](https://github.com/NVIDIA/stdexec) (default) |
| `BEMAN` | [bemanproject/execution](https://github.com/bemanproject/execution) |
| `CXX26` | The standard library's own `<execution>` (requires a C++26 standard library that ships P2300) |

```bash
cmake -S . -B build -DCOIO_SENDERS_BACKEND=NVIDIA
```

The choice is propagated to consumers through the `COIO_EXECUTION_USE_*` public compile definition, so no source changes are needed when switching.

## Using coio from your project

Add coio as a subdirectory (or via [CPM](https://github.com/cpm-cmake/CPM.cmake) / `FetchContent`) and link the target:

```cmake
add_subdirectory(coio)          # or CPMAddPackage / FetchContent
target_link_libraries(app PRIVATE coio::coio)
```

`install()` rules are provided, so a packaged build with `find_package(coio)` also works.

## A first program

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include <iostream>

using namespace std::chrono_literals;

auto greet(coio::time_loop::scheduler sched) -> coio::task<> {
    coio::timer timer{sched};
    co_await timer.async_wait(1s);
    std::cout << "hello, coio!\n";
}

auto main() -> int {
    coio::time_loop context;
    coio::this_thread::sync_wait(coio::when_all(
        greet(context.get_scheduler()),
        [&context]() -> coio::task<> { context.run(); co_return; }()
    ));
}
```

What happens here:

1. `coio::time_loop` is a portable [execution context](execution/contexts.md) with a timer queue.
2. `greet` is a [`task`](coroutines/task.md) — a lazily-started coroutine that is also a sender.
3. `co_await timer.async_wait(1s)` suspends the coroutine; the timer completes on the context's consumer thread.
4. `context.run()` drives the loop until no work remains; `sync_wait` blocks `main` until everything finishes.

## Choosing an execution context

| Context | Platform | Use it for |
|---------|----------|-----------|
| [`time_loop`](execution/time-loop.md) | portable | scheduling, timers, CPU-bound task orchestration |
| [`epoll_context`](execution/epoll.md) | Linux | sockets and pipes via readiness-based I/O |
| [`uring_context`](execution/uring.md) | Linux (kernel 5.19+) | sockets, pipes **and files** via io_uring |
| [`iocp_context`](execution/iocp.md) | Windows | sockets, pipes and files via IOCP |

All contexts share the same driving API (`run`/`poll` family) and threading contract — see [Execution Contexts](execution/contexts.md).

## Next steps

- [Concepts](concepts.md) — how senders, coroutines, and cancellation fit together.
- [Sockets](net/sockets.md) — TCP/UDP networking, including the concurrency rules you must follow.
- The `examples/` directory — echo servers (single-thread, context-pool, with-timeout variants), an HTTP server, `cat`/`cp` file utilities, and more.
