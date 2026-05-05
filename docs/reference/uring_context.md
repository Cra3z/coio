# coio::uring_context (Linux)

**Header:** `<coio/asyncio/uring_context.h>`

**Platform:** Linux only. Requires [liburing](https://github.com/axboe/liburing).

An execution context backed by **io_uring**. Includes all `time_loop` APIs plus io_uring-based asynchronous I/O.

## Base Class

Inherits from `detail::loop_base<uring_context>`, which provides the same event loop interface as `time_loop`.

## Member Types

| Type | Description |
|------|-------------|
| `scheduler` | The associated I/O scheduler. Models `io_scheduler`. |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `uring_context()` | Default constructor. Creates an io_uring with 1024 entries. |
| `explicit uring_context(unsigned entries)` | Constructs with a custom ring size. |
| `explicit uring_context(unsigned entries, std::pmr::memory_resource&) noexcept` | Constructs with custom ring size and memory resource. |
| `~uring_context()` | Destructor. |

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

### io_uring Access

| Name | Description |
|------|-------------|
| `get_uring() -> io_uring*` | Returns a pointer to the underlying `io_uring` instance. |

## `uring_context::scheduler`

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
| `make_io_object(int fd)` | Creates an `io_object` wrapping a native file descriptor. |
| `schedule_io(io_object&, Sexpr)` | Returns a sender that performs the I/O operation described by `Sexpr`. |

### `scheduler::io_object`

| Name | Description |
|------|-------------|
| `io_object(uring_context&, int fd)` | Constructor. Registers `fd`. |
| *(move-only)* | Move constructible and move assignable. |
| `get_io_scheduler()` | Returns the associated `scheduler`. |
| `native_handle() -> int` | Returns the raw file descriptor. |
| `release() -> int` | Releases ownership, returns the fd. |
| `cancel()` | Cancels pending I/O. |

## Example

```cpp
#include <coio/asyncio/uring_context.h>
#include <coio/asyncio/file.h>
#include <coio/asyncio/io.h>

int main() {
    coio::uring_context context{256};

    coio::this_thread::sync_wait(coio::when_all(
        []() -> coio::task<> {
            coio::stream_file file{
                co_await coio::execution::read_env(
                    coio::execution::get_scheduler),
                "/etc/hostname",
                coio::stream_file::read_only
            };
            char buf[256];
            auto n = co_await file.async_read_some(
                coio::as_writable_bytes(buf));
            std::cout << std::string_view{buf, n};
        }(),
        [&context]() -> coio::task<> {
            context.run(); co_return;
        }()
    ));
}
```

## Comparison: epoll vs io_uring

| Characteristic | `epoll_context` | `uring_context` |
|---------------|-----------------|-----------------|
| Readiness-based / Completion-based | readiness (needs syscall for I/O) | completion (I/O executed by kernel) |
| File I/O support | No | Yes |
| Dependency | Kernel built-in | liburing |
| Performance | Good for network I/O | Better for high-throughput / file I/O |
