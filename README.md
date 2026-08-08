# coio

---

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

A C++ **asynchronous I/O** library based on [sender/receiver model](https://wg21.link/P2300)

<details>
<summary> what's sender/receiver? </summary>

* [P2300 - std::execution](https://wg21.link/p2300): Senders proposal to C++ Standard
* [What are Senders Good For, Anyway?](https://ericniebler.com/2024/02/04/what-are-senders-good-for-anyway/): Demonstrates the value of a standard async programming model by wrapping a C-style async API in a sender
</details>

## Features

- **Sender/Receiver model** — Composable asynchronous algorithms via `std::execution`
- **Coroutine types** — `task<T, Allocator, Scheduler>` and `generator<Ref, Val, Allocator>` for async computations and lazy sequences
- **Execution contexts** — `time_loop`, `epoll_context`, `uring_context` and `iocp_context`
- **Networking** — TCP/UDP sockets with sync and async operations
- **Synchronization** — `async_mutex`, `async_semaphore`, `async_latch`
- **Utilities** — Timers, concurrent queues, signal handling

> [!NOTE]
> Some network and async-io facilities are currently only implemented using epoll and io_uring on Linux (the io_uring backend requires kernel 5.19 or newer), and IOCP on Windows.

## Build and Install

### Requirements
- **C++20**/**C++23** compatible compiler
- CMake 3.26+

### Build Options
- `COIO_BUILD_EXAMPLES` (`ON`/`OFF`, default `OFF`) - Build example programs
- `COIO_BUILD_TESTS` (`ON/OFF`, default `OFF`) - Build [**doctest**](https://github.com/doctest/doctest)-based tests
- `COIO_BUILD_WITH_ASAN` (`ON`/`OFF`, default `OFF`) - Whether to enable **AddressSanitizer**
- `COIO_BUILD_WITH_TSAN` (`ON`/`OFF`, default `OFF`) - Whether to enable **ThreadSanitizer**
- `COIO_BUILD_WITH_UBSAN` (`ON`/`OFF`, default `OFF`) - Whether to enable **UndefinedBehaviorSanitizer**
- `COIO_SENDERS_BACKEND` (`NVIDIA`/`BEMAN`/`CXX26`, default `NVIDIA`) - Which **std::execution** implementation to use:
  - `NVIDIA` - [NVIDIA/stdexec](https://github.com/NVIDIA/stdexec) implementation
  - `BEMAN` - [bemanproject/execution](https://github.com/bemanproject/execution) implementation  
  - `CXX26` - Standard library implementation

### Dependencies
- [liburing](https://github.com/axboe/liburing) (only if using `uring_context`)
- [NVIDIA/stdexec](https://github.com/NVIDIA/stdexec) (only if using `NVIDIA` std::execution implement)
- [bemanproject/execution](https://github.com/bemanproject/execution) (only if using `BEMAN` std::execution implement)

### Basic Build
```shell
cmake -S . -B <build directory>
cmake --build <build directory>
```

### Build with Examples
```shell
cmake -S . -B <build directory> -DCOIO_BUILD_EXAMPLES=ON
cmake --build <build directory>
```

### Build and Run Tests
```shell
cmake -S . -B <build directory> -DCOIO_BUILD_TESTS=ON
cmake --build <build directory>
ctest --test-dir <build directory>
```

### Install
```shell
cmake --install <build directory> --prefix <install directory>
```

### CMake Usage
If coio is already installed, you can import it as follows:
```cmake
find_package(coio REQUIRED)
target_link_libraries(<your-target> coio::coio)
```
However, it is highly recommended to use [CPM](https://github.com/cpm-cmake/CPM.cmake):
```cmake
CPMFindPackage(
    NAME coio
    GITHUB_REPOSITORY Cra3z/coio
    GIT_TAG main
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
    "COIO_BUILD_EXAMPLES OFF"
)
target_link_libraries(<your-target> coio::coio)
```

### Example
Implement a TCP echo server:
```c++
#include <iostream>
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>

#if COIO_OS_LINUX
#if COIO_HAS_IO_URING
#include <coio/asyncio/uring_context.h>
using io_context = coio::uring_context;
#else
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#endif
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#else
#error "unsupported!"
#endif

using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

auto handle_connection(tcp_socket socket) -> io_context::task<> try {
    char buffer[1024];
    while (true) {
        const auto length = co_await socket.async_read_some(coio::as_writable_bytes(buffer));
        auto [ec, n] = co_await coio::async_write(socket, coio::as_bytes(buffer, length));
        if (ec) {
            if (ec == std::errc::operation_canceled) {
                co_await coio::just_stopped();
            }
            else {
                co_await coio::just_error(ec);
            }
        }
    }
}
catch (const std::system_error& e) {
    std::cerr << "connetion error: " << e.what() << '\n';
}

auto start_server(coio::async_scope& scope) -> io_context::task<> try {
    io_context::scheduler sched = co_await coio::read_scheduler();
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    while (true) {
        scope.spawn_on(sched, handle_connection(co_await acceptor.async_accept()));
    }
}
catch (const std::system_error& e) {
    std::cerr << "acceptor error: " << e.what() << '\n';
}

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), start_server(scope));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
```

### Usage & Document

- [API Reference](https://cra3z.github.io/coio/)
- [More Examples](examples)
