# coio::iocp_context (Windows)

**Header:** `<coio/asyncio/iocp_context.h>`

**Platform:** Windows only.

An execution context backed by **I/O Completion Ports** (IOCP). Includes all `time_loop` APIs plus IOCP-based asynchronous I/O.

## Base Class

Inherits from `detail::loop_base<iocp_context>`, which provides the same event loop interface as `time_loop`.

## Member Types

| Type | Description |
|------|-------------|
| `scheduler` | The associated I/O scheduler. Models `io_scheduler`. |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `iocp_context()` | Default constructor. Uses `std::pmr::get_default_resource()`. |
| `explicit iocp_context(std::pmr::memory_resource&) noexcept` | Constructs with a custom memory resource. |
| `~iocp_context()` | Destructor. |

### Context Operations (inherited)

| Name | Description |
|------|-------------|
| `get_scheduler() noexcept` | Returns the associated `scheduler`. Thread-safe. |
| `get_allocator() const noexcept` | Returns a `std::pmr::polymorphic_allocator<>`. |
| `request_stop()` | Signals stop. Thread-safe. |
| `poll_one() -> bool` | Non-blocking, process one item. |
| `poll() -> std::size_t` | Non-blocking, process all ready items. |
| `run_one() -> bool` | Blocking, process one item. |
| `run() -> std::size_t` | Blocking, process until idle or stopped. |

## `iocp_context::scheduler`

### Scheduler Operations

| Name | Description |
|------|-------------|
| `schedule()` | Returns a sender completing on this context. |
| `schedule_after(duration)` | Returns a sender completing after duration. |
| `schedule_at(time_point)` | Returns a sender completing at time point. |
| `static now() noexcept` | Current time. |

### I/O Operations

| Name | Description |
|------|-------------|
| `make_io_object(HANDLE)` | Creates an `io_object` from a Windows `HANDLE`. |
| `make_io_object(socket_native_handle_type)` | Creates an `io_object` from a socket handle. |
| `schedule_io(io_object&, Sexpr)` | Returns a sender that performs the I/O operation described by `Sexpr`. |
| `transform_sexpr(io_object&, Sexpr)` | Converts stream operations to random-access operations for IOCP compatibility. |

### `scheduler::io_object`

| Name | Description |
|------|-------------|
| `io_object(iocp_context&, HANDLE)` | Constructor. Associates the handle with IOCP. |
| *(move-only)* | Move constructible and move assignable. |
| `get_io_scheduler()` | Returns the associated `scheduler`. |
| `native_handle() -> handle_wrapper` | Returns the handle wrapper. |
| `release() -> handle_wrapper` | Releases ownership. |
| `cancel()` | Cancels pending I/O. |
| `file_resize(size_t)` | Resizes the file associated with the handle. |
| `file_seek(size_t, seek_whence) -> size_t` | Seeks to a position. |
| `file_read(span<byte>) -> size_t` | Synchronous read. |
| `file_write(span<const byte>) -> size_t` | Synchronous write. |

### `io_object::handle_wrapper`

A wrapper around `HANDLE` that provides implicit conversions:

| Conversion | Description |
|------------|-------------|
| `operator HANDLE()` | Implicit conversion to `HANDLE`. |
| `operator socket_native_handle_type()` | Implicit conversion to `SOCKET`. |

## Example

```cpp
#include <coio/asyncio/iocp_context.h>
#include <coio/net/tcp.h>
#include <coio/net/socket.h>
#include <coio/asyncio/io.h>

int main() {
    coio::iocp_context context;
    auto sched = context.get_scheduler();

    coio::tcp::acceptor acceptor{sched,
        coio::endpoint{coio::ipv4_address::any(), 8080}};

    std::println("Listening on {}", acceptor.local_endpoint());

    coio::async_scope scope;
    scope.spawn([](auto sched, auto& scope, auto& acceptor) -> coio::task<> {
        while (true) {
            auto client = co_await acceptor.async_accept();
            scope.spawn([](auto sock) -> coio::task<> {
                char buf[1024];
                while (true) {
                    auto n = co_await sock.async_read_some(
                        coio::as_writable_bytes(buf));
                    if (n == 0) break;
                    co_await coio::async_write(sock,
                        coio::as_bytes(buf, n));
                }
            }(std::move(client)));
        }
    }(sched, scope, acceptor));

    context.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## IOCP-specific Features

The IOCP backend automatically handles:

- **Stream-to-Random-Access transformation**: Windows IOCP uses file-position-based I/O; the scheduler transforms stream operations (`read_some`/`write_some`) into random-access operations internally.
- **Handle and SOCKET integration**: Both `HANDLE` and `SOCKET` types are supported seamlessly via `handle_wrapper`.
- **File operations**: Direct `file_read`/`file_write`/`file_seek`/`file_resize` are available on `io_object`.
