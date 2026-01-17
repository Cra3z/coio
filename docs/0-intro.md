# Introduction

## What is coio?

**coio** (coroutine input/output) is a modern C++20 library that provides fundamental building blocks for asynchronous programming using C++20 coroutines. It offers:

- **Coroutine types**: `task`, `shared_task`, and `generator` for expressing asynchronous computations and lazy sequences.
- **Execution contexts**: `run_loop` for basic scheduling, `epoll_context` and `uring_context` for Linux async I/O.
- **Networking**: TCP/UDP sockets with both synchronous and asynchronous operations (Linux only).
- **Synchronization primitives**: `async_mutex`, `async_semaphore`, `async_latch` for coordinating coroutines.
- **Utilities**: timers, concurrent queues, signal handling, and more.

## Platform Support

| Feature | Linux | Windows | macOS |
|---------|-------|---------|-------|
| Core coroutine types (`task`, `shared_task`, `generator`) | ✅ | ✅ | ✅ |
| `run_loop` execution context | ✅ | ✅ | ✅ |
| Synchronization primitives | ✅ | ✅ | ✅ |
| `epoll_context` | ✅ | ❌ | ❌ |
| `uring_context` | ✅ | ❌ | ❌ |
| Networking (TCP/UDP) | ✅ | ❌ | ❌ |

> **Note**: Async I/O and networking are currently implemented only on Linux using epoll and io_uring.

## Quick Example

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>

using namespace std::chrono_literals;

// An async function that waits 1 second and returns 42
auto async_computation(coio::run_loop::scheduler sched) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(1s);
    co_return 42;
}

int main() {
    coio::run_loop context;
    
    // Run the async computation and wait for result
    auto [value] = coio::sync_wait(coio::when_all(
        async_computation(context.get_scheduler()),
        [&context]() -> coio::task<> {
            context.run();
            co_return;
        }()
    )).value();
    // value == 42
}
```

## Design Philosophy

1. **Header-only core**: Most functionality is available as header-only, making integration easy.
2. **Zero-overhead abstractions**: Leverages C++20 coroutines for efficient stack-less coroutines.
3. **Composable**: All async operations return awaitables that can be composed with `when_all`, `then`, etc.
4. **Optional P2300 integration**: Can integrate with `std::execution` (P2300) when enabled via `COIO_ENABLE_SENDERS`.

## Requirements

- **C++20 compatible compiler**:
  - GCC 11+ (recommended: GCC 13+)
  - Clang 13+ (recommended: Clang 17+)
  - MSVC 2019+ (19.28+)
- **CMake 3.26+**
- **Linux kernel 5.1+** (for io_uring support)
- **liburing** (for io_uring backend)

## Building and Installation

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Install (optional)
cmake --install build --prefix /usr/local
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `COIO_ENABLE_SENDERS` | `OFF` | Enable P2300 std::execution integration |
| `COIO_BUILD_EXAMPLES` | `ON` | Build example programs |
| `COIO_BUILD_TESTS` | `ON` | Build test suite |

### Using coio in Your Project

```cmake
find_package(coio REQUIRED)
target_link_libraries(your_target PRIVATE coio::coio)
```