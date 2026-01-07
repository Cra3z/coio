en | [zh-cn](README.zh-CN.md)  
# coio (coroutine input/ouput)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

coio provides some basic library facilities for *C++20 coroutines*.  

<details>
<summary> what's C++20 coroutine? </summary>

* [https://en.cppreference.com/w/cpp/language/coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
* [https://lewissbaker.github.io/2017/09/25/coroutine-theory](https://lewissbaker.github.io/2017/09/25/coroutine-theory)
* [https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
</details>

> [!NOTE]
> Some network and async-io facilities are currently only implemented using epoll and io_uring on linux.

## Build and Install

### Requirements
- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- CMake 3.26+

### Build Options
- `COIO_BUILD_EXAMPLES` (ON/OFF, default OFF) - Build example programs
- `COIO_ENABLE_SENDERS` (ON/OFF, default OFF) - Enable std::execution (P2300) support
- `COIO_SENDERS_BACKEND` (NVIDIA/BEMAN/CXX26, default NVIDIA) - Which std::execution implementation to use:
  - `NVIDIA` - NVIDIA/stdexec implementation
  - `BEMAN` - bemanproject/execution implementation  
  - `CXX26` - Standard library implementation (C++26+)

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

### Build with std::execution Support
```shell
cmake -S . -B <build directory> -DCOIO_ENABLE_SENDERS=ON -DCOIO_SENDERS_BACKEND=NVIDIA
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

## Public API

### Coroutine Types

#### `task<T, Alloc>` (`#include <coio/task.h>`)
A lazily-started, move-only coroutine type representing an asynchronous computation that produces a value of type `T`.
- Template parameters: `T` (result type, default `void`), `Alloc` (allocator type, default `void`)
- Awaitable: yes (move-only)

#### `shared_task<T, Alloc>` (`#include <coio/task.h>`)
A lazily-started, reference-counted coroutine type that can be awaited multiple times from different coroutines.
- Template parameters: `T` (result type, default `void`), `Alloc` (allocator type, default `void`)
- Awaitable: yes (copyable)

#### `generator<Ref, Val, Alloc>` (`#include <coio/generator.h>`)
A synchronous generator that lazily produces a sequence of values using `co_yield`.
- Template parameters: `Ref` (reference type), `Val` (value type, default deduced), `Alloc` (allocator type, default `void`)
- Models: `std::ranges::view_interface`
- Supports recursive generation via `elements_of`

### Execution Context & Schedulers

#### `run_loop` (`#include <coio/execution_context.h>`)
A basic execution context for running coroutines.
- `get_scheduler()` - get the associated scheduler
- `run()` / `run_one()` - run ready operations (blocking)
- `poll()` / `poll_one()` - run ready operations (non-blocking)
- `request_stop()` - request to stop the run loop

#### `epoll_context` (`#include <coio/asyncio/epoll_context.h>`) [Linux only]
Similar to `run_loop`, but based on epoll to support asynchronous I/O operations.

#### `uring_context` (`#include <coio/asyncio/uring_context.h>`) [Linux only]
Similar to `run_loop`, but based on io_uring to support asynchronous I/O operations.

#### `work_guard<ExecutionContext>` (`#include <coio/execution_context.h>`)
RAII guard that keeps an execution context running by maintaining an outstanding work count.

#### `inline_scheduler` (`#include <coio/schedulers.h>`)
A scheduler that executes work immediately inline without suspending.

#### Scheduler CPOs (`#include <coio/schedulers.h>`)
- `schedule(scheduler)` - get a sender/awaitable that completes on the scheduler
- `starts_on(scheduler, awaitable)` - start an awaitable on a specific scheduler
- `continues_on(awaitable, scheduler)` - continue execution on a specific scheduler after awaitable completes
- `on(scheduler, awaitable)` - execute awaitable entirely on a specific scheduler

### Coroutine Utilities

#### CPOs (`#include <coio/core.h>`)
- `when_all(awaitables...)` - await multiple awaitables concurrently, returns tuple of results
- `sync_wait(awaitable)` - synchronously wait for an awaitable to complete
- `then(awaitable, fn)` - chain a continuation function to an awaitable
- `just(value)` - create an awaitable that immediately produces a value
- `just_error(error)` - create an awaitable that immediately produces an error
- `just_stopped()` - create an awaitable that immediately stops
- `split(awaitable)` - convert an awaitable to a shared_task

#### `async_scope` (`#include <coio/core.h>`)
A scope for spawning and managing fire-and-forget async work.
- `spawn(awaitable)` - spawn an awaitable to run in the background
- `join()` - await completion of all spawned work

### Timer

#### `timer<Scheduler>` (`#include <coio/utils/timer.h>`)
A timer utility for scheduling delayed operations.
- `wait(duration)` - synchronous wait
- `wait_until(time_point)` - synchronous wait until time point
- `async_wait(duration)` - asynchronous wait
- `async_wait_until(time_point)` - asynchronous wait until time point

### Synchronization Primitives

#### `async_mutex` (`#include <coio/sync_primitives.h>`)
An asynchronous mutex for coroutines.
- `lock()` - asynchronously acquire the lock (awaitable)
- `try_lock()` - try to acquire the lock synchronously
- `unlock()` - release the lock
- `lock_guard()` - acquire and return an RAII guard (awaitable)

#### `async_unique_lock<AsyncMutex>` (`#include <coio/sync_primitives.h>`)
RAII lock wrapper for async mutexes.
- `lock()` - asynchronously acquire (awaitable)
- `try_lock()` - try to acquire synchronously
- `unlock()` - release the lock
- `owns_lock()` - check if lock is held

#### `async_semaphore<LeastMaxValue>` (`#include <coio/sync_primitives.h>`)
An asynchronous counting semaphore.
- `acquire()` - asynchronously acquire (awaitable)
- `try_acquire()` - try to acquire synchronously
- `release()` - release (awaitable)
- `count()` - get current count

#### `async_binary_semaphore` (`#include <coio/sync_primitives.h>`)
Alias for `async_semaphore<1>`.

#### `async_latch` (`#include <coio/sync_primitives.h>`)
An asynchronous latch for one-time synchronization.
- `count_down(n)` - decrement the counter
- `wait()` - wait for counter to reach zero (awaitable)
- `arrive_and_wait(n)` - decrement and wait (awaitable)
- `try_wait()` - check if counter reached zero

### Network

#### Address Types (`#include <coio/net/basic.h>`)
- `ipv4_address` - IPv4 address representation
- `ipv6_address` - IPv6 address representation  
- `ip_address` - variant of IPv4/IPv6 address
- `endpoint` - IP address + port combination

#### Protocol Types
- `tcp` (`#include <coio/net/tcp.h>`) - TCP protocol descriptor
  - `tcp::v4()` / `tcp::v6()` - get IPv4/IPv6 TCP protocol
  - `tcp::acceptor<IoScheduler>` - TCP acceptor socket type alias
  - `tcp::socket<IoScheduler>` - TCP stream socket type alias
  - `tcp::resolver` - TCP resolver type alias

- `udp` (`#include <coio/net/udp.h>`) - UDP protocol descriptor
  - `udp::v4()` / `udp::v6()` - get IPv4/IPv6 UDP protocol
  - `udp::socket<IoScheduler>` - UDP datagram socket type alias
  - `udp::resolver` - UDP resolver type alias

#### Socket Classes (`#include <coio/net/socket.h>`)

##### `basic_socket<Protocol, IoScheduler>`
Base socket class with common socket operations.
- `open(protocol)` / `close()` - open/close socket
- `bind(endpoint)` - bind to local endpoint
- `connect(endpoint)` - connect to remote endpoint (sync)
- `async_connect(endpoint)` - connect asynchronously (awaitable)
- `local_endpoint()` / `remote_endpoint()` - get endpoints
- `set_option(opt)` / `get_option(opt)` - socket options
- `cancel()` - cancel pending async operations
- `shutdown(how)` - shutdown send/receive

##### `basic_socket_acceptor<Protocol, IoScheduler>`
Server socket for accepting incoming connections.
- `listen(backlog)` - start listening
- `accept()` / `accept(scheduler)` - accept connection (sync)
- `async_accept()` / `async_accept(scheduler)` - accept asynchronously (awaitable)

##### `basic_stream_socket<Protocol, IoScheduler>`
Stream socket for TCP connections.
- `read_some(buffer)` / `write_some(buffer)` - sync I/O
- `receive(buffer)` / `send(buffer)` - sync I/O (alias)
- `async_read_some(buffer)` / `async_write_some(buffer)` - async I/O (awaitable)
- `async_receive(buffer)` / `async_send(buffer)` - async I/O (alias, awaitable)

##### `basic_datagram_socket<Protocol, IoScheduler>`
Datagram socket for UDP.
- `receive(buffer)` / `send(buffer)` - sync I/O
- `receive_from(buffer, endpoint)` / `send_to(buffer, endpoint)` - sync I/O with endpoint
- `async_receive(buffer)` / `async_send(buffer)` - async I/O (awaitable)
- `async_receive_from(buffer, endpoint)` / `async_send_to(buffer, endpoint)` - async I/O with endpoint (awaitable)

#### Resolver (`#include <coio/net/resolver.h>`)
##### `basic_resolver<Protocol, Scheduler>`
DNS resolver for hostname/service resolution.
- `resolve(query)` - resolve query to endpoint sequence (returns `generator<result_t>`)
- `resolve(protocol, query)` - resolve query to endpoint sequence (returns `generator<result_t>`)
- `async_resolve(query)` - asynchronously resolve query to endpoint sequence (returns `generator<result_t>`)
- `async_resolve(protocol, query)` - asynchronously resolve query to endpoint sequence (returns `generator<result_t>`)

### Async I/O Utilities

#### CPOs (`#include <coio/asyncio/io.h>`)
- `read(file, buffer)` - read entire buffer synchronously
- `write(file, buffer)` - write entire buffer synchronously
- `async_read(file, buffer)` - read entire buffer asynchronously (awaitable)
- `async_write(file, buffer)` - write entire buffer asynchronously (awaitable)
- `as_bytes(...)` - convert to `std::span<const std::byte>`
- `as_writable_bytes(...)` - convert to `std::span<std::byte>`

### Containers

#### `inplace_vector<T, N>` (`#include <coio/utils/inplace_vector.h>`)
A fixed-capacity vector with inline storage (no heap allocation).
- Capacity `N` is fixed at compile time
- Models: `std::ranges::contiguous_range`, `std::ranges::sized_range`
- Full STL vector-like interface

#### `basic_fixed_string<CharType, N>` (`#include <coio/utils/fixed_string.h>`)
A fixed-size string with compile-time capacity.
- `fixed_string<N>` - alias for `basic_fixed_string<char, N>`
- Useful as non-type template parameter

#### `conqueue<T, Alloc, Container>` (`#include <coio/utils/conqueue.h>`)
A thread-safe async blocking queue (multi-producer, multi-consumer).
- `push(value)` / `emplace(args...)` - add element (awaitable)
- `pop()` - remove and return element (awaitable)
- `try_pop()` - try to pop without blocking (awaitable, returns optional)
- `pop_all()` - remove all elements (awaitable)

#### `ring_buffer<T, Container>` (`#include <coio/utils/conqueue.h>`)
A fixed-size async ring buffer (single-producer, single-consumer).
- `push(value)` / `emplace(args...)` - add element (awaitable)
- `pop()` - remove and return element (awaitable)
- `try_pop()` - try to pop without blocking (awaitable, returns optional)

#### `inplace_ring_buffer<T, N>` (`#include <coio/utils/conqueue.h>`)
Alias for `ring_buffer<T, std::allocator<T>, inplace_vector<T, N>>`.

### Concepts

#### Coroutine Concepts (`#include <coio/detail/concepts.h>`)
- `awaiter<T, Promise>` - type can be used as a coroutine awaiter
- `awaitable<T, Promise>` - type can be used with `co_await`
- `awaitable_value<T, Promise>` - awaitable that is move constructible

#### Scheduler Concepts (`#include <coio/schedulers.h>`)
- `scheduler<T>` - type models a scheduler
- `timed_scheduler<T>` - scheduler with timing capabilities
- `io_scheduler<T>` - scheduler supporting I/O operations

#### Mutex Concepts (`#include <coio/sync_primitives.h>`)
- `basic_async_lockable<T>` - type has async `lock()` and `unlock()`
- `async_lockable<T>` - basic_async_lockable with `try_lock()`

#### I/O Concepts (`#include <coio/asyncio/io.h>`)
- `input_device<T>` - type has `read_some(buffer)`
- `output_device<T>` - type has `write_some(buffer)`
- `async_input_device<T>` - type has `async_read_some(buffer)`
- `async_output_device<T>` - type has `async_write_some(buffer)`
- `dynamic_buffer<T>` - type models a dynamic buffer