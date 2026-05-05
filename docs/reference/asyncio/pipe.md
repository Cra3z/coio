# Pipe

**Header:** `<coio/asyncio/pipe.h>`

Asynchronous pipe support for inter-coroutine or inter-process communication.

---

## `coio::pipe_reader`

A pipe read end. Models `async_input_stream_device`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `IoScheduler` | Must model `io_scheduler`. |

### Member Functions

| Name | Description |
|------|-------------|
| `read_some(span<byte>) -> size_t` | Synchronous read. |
| `async_read_some(span<byte>)` | Returns a sender of `size_t`. |

Also inherits: `is_open`, `get_io_scheduler`, `native_handle`, `close`, `release`, `cancel`.

---

## `coio::pipe_writer`

A pipe write end. Models `async_output_stream_device`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `IoScheduler` | Must model `io_scheduler`. |

### Member Functions

| Name | Description |
|------|-------------|
| `write_some(span<const byte>) -> size_t` | Synchronous write. |
| `async_write_some(span<const byte>)` | Returns a sender of `size_t`. |

Also inherits: `is_open`, `get_io_scheduler`, `native_handle`, `close`, `release`, `cancel`.

---

## `coio::make_pipe`

Creates a pipe and returns the reader/writer pair.

### Overloads

```cpp
// Same scheduler for both ends, creates native pipe
auto make_pipe(IoScheduler sched)
    -> std::pair<pipe_reader<IoScheduler>, pipe_writer<IoScheduler>>;

// Different schedulers
auto make_pipe(IoScheduler1 sched1, IoScheduler2 sched2)
    -> std::pair<pipe_reader<IoScheduler1>, pipe_writer<IoScheduler2>>;

// Wrap existing handles
auto make_pipe(IoScheduler1 sched1, native_handle_type read_handle,
               IoScheduler2 sched2, native_handle_type write_handle)
    -> std::pair<pipe_reader<IoScheduler1>, pipe_writer<IoScheduler2>>;

// Same scheduler, existing handles
auto make_pipe(IoScheduler sched,
               native_handle_type read_handle,
               native_handle_type write_handle)
    -> std::pair<pipe_reader<IoScheduler>, pipe_writer<IoScheduler>>;
```

## Example

```cpp
#include <coio/asyncio/pipe.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/epoll_context.h>

int main() {
    coio::epoll_context ctx;
    auto [reader, writer] = coio::make_pipe(ctx.get_scheduler());

    coio::async_scope scope;

    // Reader coroutine
    scope.spawn([](auto r) -> coio::task<> {
        char buf[128];
        while (true) {
            try {
                auto n = co_await r.async_read_some(
                    coio::as_writable_bytes(buf));
                std::print("{}", std::string_view{buf, n});
            } catch (const std::system_error& e) {
                if (e.code() == coio::error::eof) break;
                throw;
            }
        }
    }(std::move(reader)));

    // Writer coroutine
    scope.spawn([](auto w) -> coio::task<> {
        std::string_view msg = "Hello through the pipe!\n";
        co_await coio::async_write(w, coio::as_bytes(msg));
    }(std::move(writer)));

    ctx.run();
    coio::this_thread::sync_wait(scope.join());
}
```
