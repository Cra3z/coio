# File I/O

**Header:** `<coio/asyncio/file.h>`

coio provides `stream_file` and `random_access_file` for asynchronous file operations on Linux (`uring_context`) and Windows (`iocp_context`).

> Note: File I/O is NOT supported on `epoll_context` (epoll cannot do true async file I/O).

---

## Enumerations

### `open_mode`

Bitmask flags for file opening:

| Flag | Description |
|------|-------------|
| `read_only` | Open for reading only. |
| `write_only` | Open for writing only. |
| `read_write` | Open for reading and writing. |
| `append` | Append to the end of the file. |
| `create` | Create the file if it doesn't exist. |
| `exclusive` | Fail if the file already exists (used with `create`). |
| `truncate` | Truncate the file to zero length on open. |
| `sync_all_on_write` | Force `sync_all` after each write. |

### `seek_whence`

| Value | Description |
|-------|-------------|
| `seek_set` | Seek from the beginning of the file. |
| `seek_cur` | Seek from the current position. |
| `seek_end` | Seek from the end of the file. |

---

## `coio::stream_file`

A sequential-access file. Models `async_stream_device`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `IoScheduler` | Must model `io_scheduler`. |

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `stream_file(scheduler, zstring_view path, open_mode)` | Opens a file. |
| *(move-only)* | Move constructible and move assignable. |

#### Status

| Name | Description |
|------|-------------|
| `is_open() const -> bool` | Returns `true` if the file is open. |
| `explicit operator bool() const noexcept` | Same as `is_open()`. |
| `get_io_scheduler() -> scheduler` | Returns the associated scheduler. |
| `native_handle() -> native_handle_type` | Returns the native file handle. |

#### File Operations

| Name | Description |
|------|-------------|
| `open(zstring_view path, open_mode) -> void` | Opens a file. |
| `close() -> void` | Closes the file. |
| `release() -> native_handle_type` | Releases ownership. |
| `cancel() -> void` | Cancels pending operations. |
| `resize(size_t) -> void` | Resizes the file. |
| `size() -> size_t` | Returns the file size. |
| `seek(size_t offset, seek_whence) -> size_t` | Seeks and returns the new position. |
| `sync_all() -> void` | Flushes all buffered data to disk. |
| `sync_data() -> void` | Flushes data to disk (may not flush metadata). |

#### I/O

| Name | Description |
|------|-------------|
| `read_some(span<byte>) -> size_t` | Synchronous read. |
| `write_some(span<const byte>) -> size_t` | Synchronous write. |
| `async_read_some(span<byte>)` | Returns a sender of `size_t`. |
| `async_write_some(span<const byte>)` | Returns a sender of `size_t`. |

### Example

```cpp
#include <coio/asyncio/uring_context.h>
#include <coio/asyncio/file.h>
#include <coio/asyncio/io.h>

int main() {
    coio::uring_context ctx;
    coio::this_thread::sync_wait(coio::when_all(
        [&]() -> coio::task<> {
            coio::stream_file file{ctx.get_scheduler(),
                "/etc/hostname", coio::stream_file::read_only};
            std::println("Size: {} bytes", file.size());
            char buf[256];
            while (true) {
                try {
                    auto n = co_await file.async_read_some(
                        coio::as_writable_bytes(buf));
                    std::print("{}", std::string_view{buf, n});
                } catch (const std::system_error& e) {
                    if (e.code() == coio::error::eof) break;
                    throw;
                }
            }
        }(),
        [&]() -> coio::task<> { ctx.run(); co_return; }()
    ));
}
```

---

## `coio::random_access_file`

A random-access file. Models `async_random_access_device`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `IoScheduler` | Must model `io_scheduler`. |

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `random_access_file(scheduler, zstring_view path, open_mode)` | Opens a file. |
| *(move-only)* | Move constructible and move assignable. |

#### File Operations

Same as `stream_file`: `is_open`, `get_io_scheduler`, `native_handle`, `open`, `close`, `release`, `cancel`, `resize`, `size`, `sync_all`, `sync_data`.

#### I/O

| Name | Description |
|------|-------------|
| `read_some_at(offset, span<byte>) -> size_t` | Synchronous positional read. |
| `write_some_at(offset, span<const byte>) -> size_t` | Synchronous positional write. |
| `async_read_some_at(offset, span<byte>)` | Returns a sender of `size_t`. |
| `async_write_some_at(offset, span<const byte>)` | Returns a sender of `size_t`. |

### Example

```cpp
#include <coio/asyncio/uring_context.h>
#include <coio/asyncio/file.h>
#include <coio/asyncio/io.h>

auto copy_file(io_context& ctx, coio::zstring_view src_path,
               coio::zstring_view dst_path) -> coio::task<> {
    auto sched = ctx.get_scheduler();

    coio::random_access_file src{sched, src_path,
        coio::random_access_file::read_only};
    coio::random_access_file dst{sched, dst_path,
        coio::random_access_file::write_only
        | coio::random_access_file::create
        | coio::random_access_file::truncate};

    std::size_t total = src.size();
    dst.resize(total);

    std::byte buf[4096];
    for (std::size_t offset = 0; offset < total; ) {
        auto n = co_await src.async_read_some_at(offset,
            coio::as_writable_bytes(buf));
        co_await coio::async_write_at(dst, offset,
            coio::as_bytes(buf, n));
        offset += n;
    }
    dst.sync_all();
}
```
