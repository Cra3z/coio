# epoll_context

`coio::epoll_context` is the Linux reactor-style execution context, built on `epoll`. It extends the [shared execution-context model](contexts.md) with asynchronous I/O on file descriptors: sockets, pipes, timers, eventfds — anything `epoll` can watch.

Header: `#include <coio/asyncio/epoll_context.h>`

## Overview

`epoll_context` provides everything [`time_loop`](time-loop.md) does — scheduler, timers, `run`/`poll` family, work tracking, `request_stop` — plus an **I/O scheduler**: its `scheduler` satisfies coio's `io_scheduler` concept, so socket, pipe, and stream types from `<coio/net/...>` and `<coio/asyncio/...>` can be parameterized on it and perform their synchronous and asynchronous read/write/accept/connect operations through this context; see [the I/O model](../io/model.md).

Readiness notifications are demultiplexed by the consumer thread inside `run()`/`poll()`; completions of every kind are delivered on that thread, per the [three-channel invariant](contexts.md#the-three-channel-completion-invariant).

!!! warning "Platform requirements"
    Linux only: the header refuses to compile where `<sys/epoll.h>` is unavailable. Regular files and directories are rejected — `epoll` cannot watch them; use [`uring_context`](uring.md) for asynchronous file I/O on Linux.

## Synopsis

```cpp
namespace coio {
    class epoll_context {
    public:
        class scheduler /* : common context scheduler operations */ {
        public:
            using scheduler_concept = /*io-scheduler tag*/;

            // I/O scheduler: hosts sockets and pipes (see io/model.md)

            friend auto operator== (const scheduler&, const scheduler&) -> bool;
        };

        template<typename T = void, typename Alloc = void>
        using task = coio::task<T, Alloc, scheduler>;

        explicit epoll_context(
            std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());
        epoll_context(const epoll_context&) = delete;
        ~epoll_context();

        // plus the common execution-context interface:
        // get_scheduler, get_allocator, request_stop,
        // work_started / work_finished, run / run_one / poll / poll_one
    };
}
```

## API Reference

### Construction

```cpp
explicit epoll_context(
    std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());
```

Creates the epoll instance and an internal wake-up channel. `memory_resource` backs the context's internal allocations (per-descriptor bookkeeping, operation queues) and is exposed through `get_allocator()`; it must outlive the context.

**Throws:** `std::system_error` if the epoll instance or wake-up channel cannot be created.

### I/O: sockets and pipes

In addition to the [common scheduler operations](contexts.md#scheduler-operations), the scheduler hosts I/O: [socket](../net/sockets.md) and [pipe](../io/pipes.md) types parameterized on it perform their operations through this context. When such an object opens — or adopts — a file descriptor, the descriptor is registered with the context and the object **takes ownership**:

- The descriptor must not be a regular file or a directory: `epoll` does not support them, and registration throws `std::system_error` with `std::errc::operation_not_permitted` (message: ``the target file `fd` doesn't support epoll``).
- The descriptor is forced into **non-blocking mode** (`O_NONBLOCK` is set if not already present). Do not assume the descriptor remains blocking after handing it to the context.
- On any registration failure, the descriptor is closed before the exception propagates. On success, the owning object closes the descriptor when destroyed or `close()`d.

The user-facing I/O interface is documented in [the I/O model](../io/model.md). Async operations are automatically linked to the context's stop source, so `request_stop()` cancels them.

An I/O object must outlive all of its operations, and its senders must be connected and started before the object is closed or destroyed. On `epoll_context` in particular, `close()` returns the object's per-descriptor bookkeeping entry to an internal pool, so a stale start may silently corrupt the state of an unrelated I/O object that has since reused the entry, rather than failing cleanly with `EBADF`.

!!! note
    The reactor uses edge-triggered `epoll` internally — this is why non-blocking mode is mandatory — but edge- vs. level-triggering is not observable through the public API. What *is* part of the contract: at most one outstanding operation per direction (one read-like, one write-like) per I/O object; see [sockets — concurrency rules](../net/sockets.md).

### `task` alias

```cpp
template<typename T = void, typename Alloc = void>
using task = coio::task<T, Alloc, scheduler>;
```

A [task](../coroutines/task.md) affine to this context: awaited senders resume the coroutine on the context's consumer thread. This is the idiomatic coroutine type for code doing I/O on an `epoll_context`.

### Driving, work tracking, stopping

`run()`, `run_one()`, `poll()`, `poll_one()`, `work_started()`, `work_finished()`, `request_stop()`, `get_scheduler()` and `get_allocator()` behave exactly as specified on the [contexts page](contexts.md). The MPSC rule applies: any thread may initiate I/O; exactly one thread drives the context at a time. Destroying the context with outstanding work calls `std::terminate()`.

## Example

A minimal single-threaded TCP echo server (adapted from `examples/tcp_echo_server-single_thread.cpp`):

```cpp
#include <iostream>
#include <system_error>
#include <coio/core.h>
#include <coio/asyncio/epoll_context.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>

using tcp_socket   = coio::tcp::socket<coio::epoll_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<coio::epoll_context::scheduler>;

auto handle_connection(tcp_socket socket) -> coio::epoll_context::task<> {
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

auto start_server(coio::async_scope& scope) -> coio::epoll_context::task<> {
    auto sched = co_await coio::read_scheduler();
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    while (true) {
        scope.spawn_on(sched, handle_connection(co_await acceptor.async_accept()));
    }
}

auto main() -> int {
    coio::epoll_context context;
    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), start_server(scope));
    context.run();   // consumer thread: all completions are delivered here
    coio::this_thread::sync_wait(scope.join());
}
```

## See also

- [Execution contexts](contexts.md) — shared model: threading, `run`/`poll`, work tracking
- [uring_context](uring.md) — the proactor-style Linux backend (supports regular files)
- [The I/O model](../io/model.md) — I/O objects and their operations
- [Sockets](../net/sockets.md) — outstanding-operation limits and socket thread safety
