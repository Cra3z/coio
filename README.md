# coio

---

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

**coio** is a modern C++20 library that provides asynchronous programming facilities built on **std::execution** (sender/receiver model, [P2300](https://wg21.link/P2300)) and C++20 coroutines.

<details>
<summary> what's sender/receiver? </summary>

* [P2300 - std::execution](https://wg21.link/p2300): Senders proposal to C++ Standard
* [What are Senders Good For, Anyway?](https://ericniebler.com/2024/02/04/what-are-senders-good-for-anyway/): Demonstrates the value of a standard async programming model by wrapping a C-style async API in a sender
</details>

## Features

- **Sender/Receiver model** — Composable asynchronous algorithms via `std::execution`
- **Coroutine types** — `task<T, Alloc>` and `generator<Ref, Val>` for async computations and lazy sequences
- **Execution contexts** — `time_loop`, `epoll_context`, `uring_context` with thread-safe `run()`
- **Networking** — TCP/UDP sockets with sync and async operations (Linux)
- **Synchronization** — `async_mutex`, `async_semaphore`, `async_latch`
- **Utilities** — Timers, concurrent queues, signal handling

> [!NOTE]
> Some network and async-io facilities are currently only implemented using epoll and io_uring on linux.

## Build and Install

### Requirements
- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- CMake 3.26+

### Build Options
- `COIO_BUILD_EXAMPLES` (ON/OFF, default OFF) - Build example programs
- `COIO_SENDERS_BACKEND` (NVIDIA/BEMAN/CXX26, default NVIDIA) - Which std::execution implementation to use:
  - `NVIDIA` - NVIDIA/stdexec implementation
  - `BEMAN` - bemanproject/execution implementation  
  - `CXX26` - Standard library implementation (C++26+)

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

### Install
```shell
cmake --install <build directory> --prefix <install directory>
```

### CMake Usage
After installation, use in your CMakeLists.txt:
```cmake
find_package(coio REQUIRED)
target_link_libraries(your_target PRIVATE coio::coio)
```

### Usage & Document

- [API Reference](docs/reference.md)
- [Examples](examples/)