# uring_context

`coio::uring_context` is the Linux proactor-style execution context, built on `io_uring` (via liburing). It extends the [shared execution-context model](contexts.md) with submission-based asynchronous I/O and, unlike [`epoll_context`](epoll.md), supports asynchronous operations on regular files.

Header: `#include <coio/asyncio/uring_context.h>`

## Overview

`uring_context` provides everything [`time_loop`](time-loop.md) does — scheduler, timers, `run`/`poll` family, work tracking, `request_stop` — plus an **I/O scheduler** satisfying coio's `io_scheduler` concept. Operations are prepared as submission-queue entries; completions are reaped and delivered by the consumer thread inside `run()`/`poll()`, per the [three-channel invariant](contexts.md#the-three-channel-completion-invariant). Any thread may initiate operations (submission is internally synchronized); one thread drives the context.

!!! warning "Platform requirements"
    - Compile time: liburing headers must be available; the header refuses to compile otherwise.
    - Run time: the constructor probes the kernel's io_uring feature set and requires **Linux 5.19 or newer** (it checks for `IORING_OP_SOCKET`, which arrived together with the fd-scoped cancellation and extended-wait features coio relies on). On an older kernel, construction throws `std::system_error` with `std::errc::operation_not_supported` and the message `coio::uring_context requires Linux kernel 5.19 or newer`.

## Synopsis

```cpp
namespace coio {
    class uring_context {
    public:
        class scheduler /* : common context scheduler operations */ {
        public:
            using scheduler_concept = /*io-scheduler tag*/;

            // I/O scheduler: hosts files, sockets and pipes (see io/model.md)
        };

        template<typename T = void, typename Alloc = std::allocator<std::byte>>
        using task = coio::task<T, Alloc, scheduler>;

        uring_context();
        explicit uring_context(
            std::size_t entries,
            std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());
        uring_context(const uring_context&) = delete;
        ~uring_context();
        auto operator= (const uring_context&) -> uring_context& = delete;

        [[nodiscard]] auto get_uring() noexcept -> ::io_uring*;

        // plus the common execution-context interface:
        // get_scheduler, get_allocator, request_stop,
        // work_started / work_finished, run / run_one / poll / poll_one
    };
}
```

## API Reference

### Construction

```cpp
uring_context();
explicit uring_context(
    std::size_t entries,
    std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());
```

Initializes an `io_uring` instance with `entries` submission-queue entries (the default constructor picks an implementation-chosen default) and performs the kernel feature probe described above. `memory_resource` backs internal allocations and is exposed via `get_allocator()`; it must outlive the context.

**Throws:** `std::system_error` —

- with the underlying error if `io_uring_queue_init` fails;
- with `std::errc::value_too_large` if `entries` exceeds the maximum representable ring size;
- with `std::errc::operation_not_supported` (`coio::uring_context requires Linux kernel 5.19 or newer`) if the runtime probe fails.

!!! note
    `entries` bounds how many submissions can be in flight before the ring is full. If the ring is exhausted, initiating a new operation fails with `std::errc::no_buffer_space` delivered through the operation's error channel; size `entries` generously for highly concurrent workloads.

### I/O: files, sockets and pipes

In addition to the [common scheduler operations](contexts.md#scheduler-operations), the scheduler hosts I/O: [file](../io/files.md), [socket](../net/sockets.md) and [pipe](../io/pipes.md) types parameterized on it perform their operations through this context. When such an object opens — or adopts — a file descriptor, the object **takes ownership**: the descriptor is closed when the object is destroyed or `close()`d (which requires all of its operations to have completed first — `cancel()` uses fd-scoped io_uring cancellation while the object is still open). Any descriptor io_uring can operate on is accepted — including regular files; descriptors are *not* switched to non-blocking mode.

The user-facing I/O interface is documented in [the I/O model](../io/model.md). Async operations are automatically linked to the context's stop source, so `request_stop()` cancels them. An I/O object must outlive its operations; per-object outstanding-operation limits for sockets are specified on [the sockets page](../net/sockets.md).

### `task` alias

```cpp
template<typename T = void, typename Alloc = std::allocator<std::byte>>
using task = coio::task<T, Alloc, scheduler>;
```

A [task](../coroutines/task.md) affine to this context: awaited senders resume the coroutine on the context's consumer thread.

### `get_uring`

```cpp
[[nodiscard]] auto get_uring() noexcept -> ::io_uring*;
```

Escape hatch: the underlying liburing ring. Use it for features coio does not wrap (registered buffers, ring configuration queries, ...). Anything submitted directly must not disturb coio's entries; prefer the portable API unless you know exactly what you need.

### Driving, work tracking, stopping

`run()`, `run_one()`, `poll()`, `poll_one()`, `work_started()`, `work_finished()`, `request_stop()`, `get_scheduler()` and `get_allocator()` behave exactly as specified on the [contexts page](contexts.md). Destroying the context with outstanding work calls `std::terminate()`.

## Example

The same echo server as on the [epoll page](epoll.md), bound to `uring_context`:

```cpp
#include <iostream>
#include <system_error>
#include <coio/core.h>
#include <coio/asyncio/uring_context.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>

using tcp_socket   = coio::tcp::socket<coio::uring_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<coio::uring_context::scheduler>;

auto handle_connection(tcp_socket socket) -> coio::uring_context::task<> {
    try {
        char buffer[1024];
        while (true) {
            const auto n = co_await socket.async_read_some(coio::as_writable_bytes(buffer));
            std::size_t sent = 0;
            while (sent < n) {
                sent += co_await socket.async_write_some(coio::as_bytes(buffer + sent, n - sent));
            }
        }
    }
    catch (const std::system_error& e) {
        std::cout << "connection closed: " << e.what() << '\n';
    }
}

auto start_server(coio::async_scope& scope) -> coio::uring_context::task<> {
    auto sched = co_await coio::read_scheduler();
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    while (true) {
        scope.spawn_on(sched, handle_connection(co_await acceptor.async_accept()));
    }
}

auto main() -> int {
    coio::uring_context context{4096};   // submission-queue entries
    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), start_server(scope));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## See also

- [Execution contexts](contexts.md) — shared model: threading, `run`/`poll`, work tracking
- [epoll_context](epoll.md) — the reactor-style Linux backend
- [The I/O model](../io/model.md) — I/O objects and their operations
- [Sockets](../net/sockets.md) — outstanding-operation limits and socket thread safety
