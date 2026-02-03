# coio Reference

This document describes the public API of **coio**. It is intended to be a stable reference for headers under `include/coio/`.

- **Language level**: C++20 (the library). `std::execution` is provided by an implementation library (P2300).
- **Scope**: coio types and contracts. Standard `std::execution` algorithms are not re-documented here.
- **Platform**: Core types are portable; async I/O + networking backends are currently Linux-only.

## Contents

- [1. Conventions](#1-conventions)
- [2. Header map](#2-header-map)
- [3. Core coroutine types](#3-core-coroutine-types)
  - [3.1 task](#31-task)
  - [3.2 generator](#32-generator)
- [4. Execution contexts](#4-execution-contexts)
  - [4.1 Thread-safety model](#41-thread-safety-model)
  - [4.2 time_loop](#42-time_loop)
  - [4.3 epoll_context (Linux)](#43-epoll_context-linux)
  - [4.4 uring_context (Linux)](#44-uring_context-linux)
  - [4.5 work_guard](#45-work_guard)
- [5. Waiting & coio-specific algorithms](#5-waiting--coio-specific-algorithms)
- [6. Utilities](#6-utilities)
  - [6.1 async_scope](#61-async_scope)
  - [6.2 timer](#62-timer)
- [7. Async I/O utilities](#7-async-io-utilities)
- [8. Networking (Linux)](#8-networking-linux)
- [9. Synchronization primitives](#9-synchronization-primitives)
- [10. Error handling](#10-error-handling)
- [11. Thread safety (summary)](#11-thread-safety-summary)

---

## 1. Conventions

### Senders, awaitables, and composition

- Many coio operations are **senders** (P2300). They can be composed with `std::execution` algorithms.
- `coio::task<T, Alloc>` is both a coroutine type and a sender.
- Inside a `coio::task`, you can `co_await` a sender (coio wires sender-to-awaitable via `await_transform`).

### Stop tokens and cancellation

- Many async operations support cooperative cancellation by [stop token](https://eel.is/c++draft/thread.stoptoken).
- Cancellation follows the sender/receiver contract: cancellation completes with `set_stopped()`.

---

## 2. Header map

| Area | Header |
|------|--------|
| Core concepts + algorithms | `#include <coio/core.h>` |
| task | `#include <coio/task.h>` |
| generator | `#include <coio/generator.h>` |
| time_loop + work_guard | `#include <coio/execution_context.h>` |
| epoll backend | `#include <coio/asyncio/epoll_context.h>` |
| io_uring backend | `#include <coio/asyncio/uring_context.h>` |
| timers | `#include <coio/utils/timer.h>` |
| async_scope | `#include <coio/utils/async_scope.h>` |
| sync primitives | `#include <coio/sync_primitives.h>` |
| I/O helpers | `#include <coio/asyncio/io.h>` |
| networking basics | `#include <coio/net/basic.h>` |
| TCP/UDP descriptors | `#include <coio/net/tcp.h>`, `#include <coio/net/udp.h>` |
| sockets | `#include <coio/net/socket.h>` |
| resolver | `#include <coio/net/resolver.h>` |

---

## 3. Core coroutine types

### 3.1 task

Header: `#include <coio/task.h>`

`coio::task<T, Alloc>` is a **lazily-started, move-only coroutine type** that also models a **sender**.

**Properties**

- Move-only.
- Lazy: starts when awaited or when the sender operation state is started.
- Completion: `set_value(T)` (or `set_value()` for `T=void`), `set_error(std::exception_ptr)`, `set_stopped()`.
- Stop token: when awaited, the task inherits the stop token from the awaiting coroutine’s environment.

**Typical usage**

```cpp
auto foo() -> coio::task<int> {
    co_return 42;
}

int x = co_await foo();

auto r = coio::this_thread::sync_wait(foo());
```

**Notes**

- This reference intentionally does not document `std::execution::then/when_all/...` — see P2300.

### 3.2 generator

Header: `#include <coio/generator.h>`

`coio::generator<Ref, Val, Alloc>` is a **synchronous generator** with lazy `co_yield`.
It is same as [P2502 - std::generator](https://wg21.link/p2502) in C++23, but works in C++20.

**Properties**

- Models `std::ranges::view_interface`.
- Single-pass input iteration.
- Supports recursive generation via `coio::elements_of(range_or_generator)`.

**Example**

```cpp
auto fibonacci(std::size_t n) -> coio::generator<int> {
    int a = 0, b = 1;
    while (n--) {
        co_yield b;
        a = std::exchange(b, a + b);
    }
}
```

---

## 4. Execution contexts

### 4.1 Thread-safety model

All execution contexts (`time_loop`, `epoll_context`, `uring_context`) share these guarantees:

- `run()` / `run_one()` can be called concurrently from multiple threads.
- `poll()` / `poll_one()` can be called concurrently from multiple threads.
- `get_scheduler()` is thread-safe.
- `request_stop()` is thread-safe.

Work submitted to the context may be executed by **any** thread currently calling `run()`/`poll()`.

### 4.2 time_loop

Header: `#include <coio/execution_context.h>`

Execution context with a timer queue and a manually-driven event loop.

**Core operations**

| Method | Description |
|--------|-------------|
| `get_scheduler()` | returns a scheduler (models `std::execution::scheduler`) |
| `run()` | blocks and processes work until no work remains or stop is requested |
| `run_one()` | blocks and processes exactly one work item |
| `poll()` | processes all ready work without blocking |
| `poll_one()` | processes at most one ready work item without blocking |
| `request_stop()` | signals the context to stop processing |
| `work_started()` / `work_finished()` | manual work tracking (advanced) |

**Scheduler operations**

| Method | Description |
|--------|-------------|
| `schedule()` | returns a sender completing on this context |
| `schedule_after(duration)` | completes after duration |
| `schedule_at(time_point)` | completes at time point |
| `now()` | current time point |

### 4.3 epoll_context (Linux)

Header: `#include <coio/asyncio/epoll_context.h>`

Execution context backed by **epoll**. Includes all `time_loop` APIs plus Linux I/O scheduling.

- Recommended for general-purpose Linux network servers.
- Provides an I/O scheduler with an `io_object` that owns a file descriptor and can cancel pending operations.

### 4.4 uring_context (Linux)

Header: `#include <coio/asyncio/uring_context.h>`

Execution context backed by **io_uring**. Includes all `time_loop` APIs plus io_uring-based I/O.

- Recommended for high-performance I/O on Linux 5.1+.
- `uring_context(depth)` controls submission queue depth.

### 4.5 work_guard

Header: `#include <coio/execution_context.h>`

`coio::work_guard<ExecutionContext>` is an RAII guard that increments the context work count and keeps `run()` from returning.

---

## 5. Waiting & coio-specific algorithms

Header: `#include <coio/core.h>`

### Synchronous waiting

- `coio::this_thread::sync_wait(sender)`
- `coio::this_thread::sync_wait_with_variant(sender)`

These block the current thread until the sender completes.

### coio-specific algorithms

| Algorithm | Description |
|-----------|-------------|
| `when_any(senders...)` | completes when the first sender completes |
| `when_any_with_variant(senders...)` | variant-form result |
| `stop_when(sender, stop_token)` | attaches external cancellation to a sender |

---

## 6. Utilities

### 6.1 async_scope

Header: `#include <coio/utils/async_scope.h>`

A [scope](https://wg21.link/p3149#introduction) object for spawning background work.

- `spawn(sender)`
- `request_stop()`
- `join()` → sender completing when all work finishes

### 6.2 timer

Header: `#include <coio/utils/timer.h>`

Timer bound to a scheduler.

- `async_wait(duration)`
- `async_wait_until(time_point)`
- `cancel()`

---

## 7. Async I/O utilities

Header: `#include <coio/asyncio/io.h>`

This header provides concepts and helper functions for `read`, `write`, and delimiter-based reads.

- `read(device, buffer)` / `write(device, buffer)`
- `async_read(device, buffer, token)` / `async_write(device, buffer, token)`
- `read_until(...)` / `async_read_until(...)`

---

## 8. Networking (Linux)

Headers: `#include <coio/net/...>`

> Networking backends are currently implemented only on Linux.

### Address and endpoint types

- `ipv4_address`, `ipv6_address`, `ip_address`, `endpoint`

### Protocol descriptors

- `tcp::v4()` / `tcp::v6()`
- `udp::v4()` / `udp::v6()`

### Socket types

Header: `#include <coio/net/socket.h>`

- `basic_socket<Protocol, IoScheduler>`: open/close/bind/connect/options
- `basic_socket_acceptor<Protocol, IoScheduler>`: listen/accept
- `basic_stream_socket<Protocol, IoScheduler>`: read/write
- `basic_datagram_socket<Protocol, IoScheduler>`: send/recv, send_to/recv_from

### Concurrency rules

- Do not start multiple concurrent reads on the same socket.
- Do not start multiple concurrent writes on the same socket.
- One read + one write concurrently on the same socket is allowed.
- Do not call multiple `accept`/`async_accept` concurrently on the same acceptor.

### EOF behavior

`basic_stream_socket::read_some/async_read_some` report connection close as `coio::error::misc_errc::eof`.

---

## 9. Synchronization primitives

Header: `#include <coio/sync_primitives.h>`

- `async_mutex`
- `async_semaphore` (and `async_binary_semaphore`)
- `async_latch`

These primitives suspend coroutines instead of blocking threads.

---

## 10. Error handling

`coio::error::misc_errc` includes library-specific error codes.

- `eof`: end of stream

---

## 11. Thread safety (summary)

- Execution contexts: thread-safe `run/poll/get_scheduler/request_stop`.
- Sync primitives: safe across coroutines potentially running on different threads.
- `async_scope`: safe to `spawn()` from multiple threads.
- Sockets: follow the per-socket concurrency rules described above.