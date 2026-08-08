# iocp_context

`coio::iocp_context` is the Windows execution context, built on I/O Completion Ports. It extends the [shared execution-context model](contexts.md) with overlapped asynchronous I/O on files and sockets.

Header: `#include <coio/asyncio/iocp_context.h>`

## Overview

`iocp_context` provides everything [`time_loop`](time-loop.md) does — scheduler, timers, `run`/`poll` family, work tracking, `request_stop` — plus an **I/O scheduler** satisfying coio's `io_scheduler` concept, so [files](../io/files.md), [pipes](../io/pipes.md) and [sockets](../net/sockets.md) can be parameterized on it. Handles are associated with the completion port; the consumer thread inside `run()`/`poll()` dequeues completion packets and delivers all completions there, per the [three-channel invariant](contexts.md#the-three-channel-completion-invariant).

!!! warning "Platform requirements"
    Windows only; the header refuses to compile where IOCP is unavailable. Native handles and sockets adopted by files, pipes or sockets on this context **must be opened in overlapped mode** (`FILE_FLAG_OVERLAPPED` / `WSA_FLAG_OVERLAPPED`); associating a non-overlapped handle leads to operations that never complete or complete inline — this precondition is not checked.

## Synopsis

```cpp
namespace coio {
    class iocp_context {
    public:
        class scheduler /* : common context scheduler operations */ {
        public:
            using scheduler_concept = /*io-scheduler tag*/;

            // I/O scheduler: hosts files, pipes and sockets (see io/model.md)

            friend auto operator== (const scheduler&, const scheduler&) -> bool;
        };

        template<typename T = void, typename Alloc = void>
        using task = coio::task<T, Alloc, scheduler>;

        explicit iocp_context(
            std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());
        iocp_context(const iocp_context&) = delete;
        ~iocp_context();
        auto operator= (const iocp_context&) -> iocp_context& = delete;

        // plus the common execution-context interface:
        // get_scheduler, get_allocator, request_stop,
        // work_started / work_finished, run / run_one / poll / poll_one
    };
}
```

## API Reference

### Construction

```cpp
explicit iocp_context(
    std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());
```

Creates the completion port. The first `iocp_context` constructed in a process also initializes Winsock (`WSAStartup` requesting version 2.2) through a process-wide guard; `WSACleanup` runs automatically at process exit, so user code never calls either. `memory_resource` backs internal allocations and is exposed via `get_allocator()`; it must outlive the context.

**Throws:** `std::system_error` if `WSAStartup` or completion-port creation fails.

### I/O: files, pipes and sockets

In addition to the [common scheduler operations](contexts.md#scheduler-operations), the scheduler hosts I/O: [file](../io/files.md), [pipe](../io/pipes.md) and [socket](../net/sockets.md) types parameterized on it perform their operations through this context. When such an object opens — or adopts — a native handle or socket, the handle is associated with the completion port and the object **takes ownership**: the handle is closed by the object's destructor or `close()` (outstanding operations are cancelled first), and is closed immediately if association fails before the exception propagates.

**Throws:** `std::system_error` if associating the handle with the port fails.

Because IOCP file I/O is inherently positional, stream-style operations (`read_some`/`write_some` and their `async_` forms) on files work against an internal per-object offset: it is seeded from the handle's current file pointer when the handle is associated, each stream-style operation issues at the current offset and advances it — for asynchronous stream operations the offset is reserved and advanced at initiation — and `seek()` repositions it. Interleaving positional (`*_at`) and stream-style operations is allowed; positional ones do not move the offset.

The user-facing I/O interface is documented in [the I/O model](../io/model.md). Async operations are automatically linked to the context's stop source, so `request_stop()` cancels them. An I/O object must outlive its operations; per-object outstanding-operation limits for sockets are specified on [the sockets page](../net/sockets.md).

!!! note
    The stream-style offset is plain object state: initiating two stream-style operations concurrently on the same file object races on the offset. Keep at most one outstanding operation per direction, as with sockets.

### `task` alias

```cpp
template<typename T = void, typename Alloc = void>
using task = coio::task<T, Alloc, scheduler>;
```

A [task](../coroutines/task.md) affine to this context: awaited senders resume the coroutine on the context's consumer thread.

### Driving, work tracking, stopping

`run()`, `run_one()`, `poll()`, `poll_one()`, `work_started()`, `work_finished()`, `request_stop()`, `get_scheduler()` and `get_allocator()` behave exactly as specified on the [contexts page](contexts.md). The MPSC rule applies: any thread may initiate I/O; exactly one thread drives the context at a time. Destroying the context with outstanding work calls `std::terminate()`.

## Example

The same echo server as on the [epoll page](epoll.md), bound to `iocp_context`:

```cpp
#include <iostream>
#include <system_error>
#include <coio/core.h>
#include <coio/asyncio/iocp_context.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>

using tcp_socket   = coio::tcp::socket<coio::iocp_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<coio::iocp_context::scheduler>;

auto handle_connection(tcp_socket socket) -> coio::iocp_context::task<> {
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

auto start_server(coio::async_scope& scope) -> coio::iocp_context::task<> {
    auto sched = co_await coio::read_scheduler();
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    while (true) {
        scope.spawn_on(sched, handle_connection(co_await acceptor.async_accept()));
    }
}

auto main() -> int {
    coio::iocp_context context;    // initializes Winsock on first use
    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), start_server(scope));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## See also

- [Execution contexts](contexts.md) — shared model: threading, `run`/`poll`, work tracking
- [The I/O model](../io/model.md) — I/O objects and their operations
- [Sockets](../net/sockets.md) — outstanding-operation limits and socket thread safety
- [Files](../io/files.md) — the portable file types built on these I/O objects
