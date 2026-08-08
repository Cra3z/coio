# The I/O Object Model

Every asynchronous I/O facility in coio — sockets, files, pipes — follows a single model: an I/O **execution context** (`epoll_context`, `uring_context`, or `iocp_context`) exposes a *scheduler* that can create backend-specific **io objects**, and user-facing wrapper classes own those io objects and forward operations to them. This page describes that model and the contracts that apply to all I/O wrappers: synchronous versus asynchronous members, cancellation, `close()` semantics, lifetime rules, outstanding-operation limits, and EOF conventions.

Headers: `#include <coio/asyncio/epoll_context.h>`, `#include <coio/asyncio/uring_context.h>`, `#include <coio/asyncio/iocp_context.h>` (backends); wrappers in `<coio/asyncio/file.h>`, `<coio/asyncio/pipe.h>`, `<coio/net/socket.h>`

## Overview

The pieces fit together like this:

1. An I/O execution context (e.g. `epoll_context`) owns the OS demultiplexer and the completion queue. Its `get_scheduler()` returns a lightweight, copyable **scheduler** that models the `coio::io_scheduler` concept.
2. `scheduler.make_io_object(native_handle)` wraps a native handle (POSIX fd, Windows `HANDLE`/`SOCKET`) in a backend-specific, move-only **io object**. The io object registers the handle with the backend as needed and *takes ownership of the handle*.
3. Wrapper class templates — `basic_socket<Protocol, IoScheduler>` and friends, `stream_file<IoScheduler>`, `random_access_file<IoScheduler>`, `pipe_reader<IoScheduler>`/`pipe_writer<IoScheduler>` — are parameterized on the scheduler type and hold the io object as their single data member. All their data-path members forward to it.

```text
epoll_context / uring_context / iocp_context
        │ get_scheduler()
        ▼
    scheduler ──── make_io_object(handle) ───► io object   (owns the native handle)
                                                   ▲
                                                   │ owned by
                              stream_file / basic_stream_socket / pipe_reader / ...
```

The `io_scheduler` concept (from `<coio/core.h>`) is what the wrapper templates require:

```cpp
template<typename Scheduler>
concept io_scheduler =
    execution::scheduler<Scheduler> and
    std::derived_from<typename std::remove_cvref_t<Scheduler>::scheduler_concept,
                      detail::io_scheduler_tag>;
```

It is satisfied by `epoll_context::scheduler`, `uring_context::scheduler` and `iocp_context::scheduler`, but not by plain `time_loop` schedulers.

### Native handle types

| Platform | Files / pipes (`detail::file_native_handle_type`) | Sockets (`detail::socket_native_handle_type`) |
|----------|---------------------------------------------------|-----------------------------------------------|
| Linux    | `int` (invalid: `-1`)                              | `int` (invalid: `-1`)                          |
| Windows  | `void*` (i.e. `HANDLE`; invalid: `INVALID_HANDLE_VALUE`) | `UINT_PTR` (i.e. `SOCKET`; invalid: `SOCKET(-1)`) |

On Linux, `make_io_object(int fd)` produces one io object type used for both files and sockets. On Windows, `iocp_context::scheduler` has two overloads: `make_io_object(HANDLE)` returns a *file object* and `make_io_object(SOCKET)` returns a *socket object*; both associate the handle with the completion port at construction (the handle must have been opened for overlapped I/O).

## Synopsis

Every io object provides (modulo backend-specific subsets) the following surface; wrappers re-export the parts that make sense for their category:

```cpp
// common to all backends
auto get_io_scheduler() const noexcept -> scheduler;
auto native_handle() const noexcept -> /* platform handle type */;
auto close() -> void;             // cancel + release the handle
auto cancel() -> void;            // cancel outstanding async operations

// synchronous data path (blocking; throw std::system_error on failure)
auto read_some(std::span<std::byte>) -> std::size_t;
auto write_some(std::span<const std::byte>) -> std::size_t;
auto read_some_at(std::size_t offset, std::span<std::byte>) -> std::size_t;
auto write_some_at(std::size_t offset, std::span<const std::byte>) -> std::size_t;
auto receive(std::span<std::byte>) -> std::size_t;
auto send(std::span<const std::byte>) -> std::size_t;
auto receive_from(std::span<std::byte>) -> std::pair<endpoint, std::size_t>;
auto send_to(std::span<const std::byte>, const endpoint&) -> std::size_t;
auto seek(std::size_t offset, seek_whence) -> std::size_t;
auto resize(std::size_t new_size) -> void;

// asynchronous data path (each returns a sender)
auto async_read_some(std::span<std::byte>) noexcept;
auto async_write_some(std::span<const std::byte>) noexcept;
auto async_read_some_at(std::size_t, std::span<std::byte>) noexcept;
auto async_write_some_at(std::size_t, std::span<const std::byte>) noexcept;
auto async_receive(std::span<std::byte>) noexcept;
auto async_send(std::span<const std::byte>) noexcept;
auto async_receive_from(std::span<std::byte>) noexcept;
auto async_send_to(std::span<const std::byte>, const endpoint&) noexcept;
auto async_accept() noexcept;
auto async_connect(const endpoint&) noexcept;
```

All buffers are single contiguous `std::span`s. There is no scatter-gather buffer sequence type in coio.

## API Reference

### Synchronous members

Synchronous data-path members (`read_some`, `receive`, `send_to`, ...) block the calling thread until the operation completes, return their result directly, and **throw `std::system_error`** on failure. They never touch the context's completion queue, so they may be called without a running consumer thread.

!!! note "Linux: sync ops on nonblocking descriptors"
    `epoll_context` forces `O_NONBLOCK` on every descriptor it adopts (see platform notes below). Synchronous operations still behave as blocking calls: on `EAGAIN`/`EWOULDBLOCK` they internally `poll(2)` the descriptor for readiness and retry, so the calling thread blocks until the operation can proceed.

### Asynchronous members

Members named `async_*` are **lazy sender factories**: calling one performs no I/O; the returned sender initiates the operation when its operation state is `start()`ed (e.g. when `co_await`ed inside a task). Base completion signatures are:

- `set_value(payload)` — `std::size_t` bytes transferred for reads/writes, `(endpoint, std::size_t)` for `async_receive_from`, a native handle (wrapped into a socket by `basic_socket_acceptor`) for `async_accept`, nothing for `async_connect`;
- `set_error(std::error_code)` — OS failure;
- `set_stopped()` — the operation was cancelled.

Completions are always delivered on the context's consumer thread (the thread inside `run()`/`poll()`), never inline on the initiating thread.

### Cancellation

Three mechanisms cancel outstanding asynchronous operations; all of them complete the affected operations with `set_stopped()`:

1. **Context stop token.** Every `async_*` sender an io object returns is wrapped in `stop_when(op, context_stop_token)` at construction. `context.request_stop()` therefore cancels all in-flight I/O on that context. A stop request also propagates from the receiver's environment as usual (`stop_when` composes).
2. **`cancel()`** on the io object / wrapper: cancels the operations currently outstanding on that one object (`CancelIoEx(handle, nullptr)` on IOCP, `IORING_ASYNC_CANCEL_ALL` for the fd on io_uring, unhooking the registered in/out waiters on epoll).
3. **`close()`**, which performs `cancel()` first (see below).

A stop request never overwrites a real result: if the target operation completes successfully (or with an ordinary error) before the cancellation wins, the receiver still gets `set_value`/`set_error`.

### `close()`

`close()` cancels outstanding operations, releases the native handle back to the OS, and resets the object to the not-open state. It throws `std::system_error` if the OS close fails. Per backend:

| Backend | What `close()` does |
|---------|---------------------|
| `epoll_context` | Cancels the registered in/out operations (they complete with `set_stopped`), removes the fd from the epoll set (`EPOLL_CTL_DEL`), returns the per-descriptor bookkeeping entry to an internal pool, then `close(fd)`. |
| `uring_context` | Submits an `IORING_ASYNC_CANCEL_ALL` cancellation for the fd, then `close(fd)`. |
| `iocp_context` | `CancelIoEx(handle, nullptr)`, then `CloseHandle` (files/pipes) or `closesocket` (sockets). |

Destroying a wrapper object implicitly closes it.

### Lifetime

An I/O object must outlive all of its operations. A sender obtained from an I/O object (`async_read_some`, `async_receive`, ...) must be connected and started **before** the object is closed or destroyed; starting it afterwards is undefined behavior. On `epoll_context` in particular, `close()` returns the object's per-descriptor bookkeeping entry to an internal pool, so a stale start may silently corrupt the state of an unrelated I/O object that has since reused the entry, rather than failing cleanly with `EBADF`.

!!! warning
    "Started before close" is a hard precondition, not a checked error. Keep the wrapper alive until every sender obtained from it has completed (structured concurrency — `co_await`, `when_all`, `async_scope::join()` — makes this automatic).

### Outstanding-operation limits

Per I/O object (Asio-style): at most **one outstanding read-direction operation and one outstanding write-direction operation** at a time; one of each may overlap. Acceptors allow at most one outstanding accept. Initiating a second operation in the same direction before the first completes is undefined behavior (`epoll_context` asserts on it in debug builds). See [Sockets — concurrency rules](../net/sockets.md#concurrency-rules) for the full rules and examples; they apply equally to files and pipes.

### EOF conventions

- A backend read that transfers **0 bytes into a non-empty buffer** means end-of-stream. The stream wrappers (`basic_stream_socket`, `stream_file`, `random_access_file`, `pipe_reader`) map this to `coio::error::eof`: synchronous members throw `std::system_error{coio::error::eof}`; `async_read_some` / `async_read_some_at` complete with `set_error(coio::error::eof)`.
- On Windows, `ERROR_HANDLE_EOF` and `ERROR_BROKEN_PIPE` are mapped to the same EOF path (`ERROR_BROKEN_PIPE` is how a pipe reports its writer closing, matching POSIX `read() == 0`).
- Datagram sockets have no EOF concept; a 0-byte receive is a valid empty datagram.
- Stream-oriented I/O (files, pipes, stream sockets): a read or write with an **empty buffer** is a no-op that completes immediately with 0, without touching the OS, and is never treated as EOF (asio parity).
- Datagram sockets: zero-length operations are **real**. An empty `send`/`send_to` transmits an empty datagram; a zero-length receive waits for and consumes a datagram.

### Platform notes

!!! warning "epoll rejects regular files"
    `epoll_context::scheduler::make_io_object` calls `fstat` on the descriptor and **throws `std::system_error` (`operation_not_permitted`)** for regular files and directories, closing the descriptor — `epoll(7)` cannot wait on them. Use `uring_context` for file I/O on Linux. Pipes, FIFOs, character devices and sockets are fine. The constructor also sets `O_NONBLOCK` on the adopted descriptor if not already set.

!!! note "IOCP stream-file offset is reserved at initiation (Asio-style)"
    Windows overlapped files have no kernel file position, so the IOCP file object keeps its own offset for `stream_file`. `async_read_some`/`async_write_some` **capture and advance that offset when the sender is created**, not when it completes. Consequently a sender that is built but never started still consumes its offset range, and the sequential-stream illusion only holds if you respect the one-outstanding-op-per-direction limit and start senders in the order you create them.

!!! note "Linux synchronous socket ops poll internally"
    Because descriptors adopted by `epoll_context` are switched to `O_NONBLOCK`, the synchronous socket members (`receive`, `send`, `receive_from`, `send_to`, `accept`, `connect`) emulate blocking behavior by retrying after an internal `poll(2)` when the call would block.

## Example

```cpp
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_socket = coio::tcp::socket<io_context::scheduler>;

auto client() -> io_context::task<> {
    // the scheduler creates the io object; the wrapper owns it
    io_context::scheduler sched = co_await coio::read_scheduler();
    tcp_socket socket{sched};

    co_await socket.async_connect({coio::ipv4_address::loopback(), 8086});

    char buffer[1024];
    const std::size_t n = co_await socket.async_read_some(coio::as_writable_bytes(buffer));
    // `socket` stays alive until the operation completes: lifetime rule satisfied
    co_await socket.async_write_some(coio::as_bytes(buffer, n));
}   // ~tcp_socket: cancels outstanding ops, closes the handle

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), client());
    context.run();                       // consumer thread: completions run here
    coio::this_thread::sync_wait(scope.join());
}
```

## See also

- [Files](files.md) — `stream_file`, `random_access_file`
- [Pipes](pipes.md) — `pipe_reader`, `pipe_writer`, `make_pipe`
- [I/O algorithms](algorithms.md) — `async_read`, `async_write`, `async_read_until`, device concepts
- [Sockets](../net/sockets.md) — socket wrappers and the full concurrency rules
- [Execution contexts](../execution/contexts.md), [epoll](../execution/epoll.md), [io_uring](../execution/uring.md), [IOCP](../execution/iocp.md)
- [Error handling](../error-handling.md), [Thread safety](../thread-safety.md)
