# Files

coio provides two file wrappers over the [I/O object model](model.md): `stream_file`, which maintains an internal file position that advances with each read or write, and `random_access_file`, which has no internal position — every operation names an explicit offset. Both offer synchronous and asynchronous (sender-returning) operations.

Header: `#include <coio/asyncio/file.h>`

## Overview

| Type | Access pattern | Data-path members |
|------|----------------|--------------|
| `stream_file<IoScheduler>` | Sequential; internal position | `read_some` / `write_some` / `async_read_some` / `async_write_some`, plus `seek` |
| `random_access_file<IoScheduler>` | Positional; caller-supplied offset | `read_some_at` / `write_some_at` / `async_read_some_at` / `async_write_some_at` |

Both share a common base surface (`open`, `close`, `cancel`, `is_open`, `native_handle`, `get_io_scheduler`, `resize`, `size`, `sync_all`, `sync_data`). The native handle type is `int` on Linux and `HANDLE` (`void*`) on Windows.

!!! warning "Platform support"
    Regular files are supported on **`uring_context`** (Linux) and **`iocp_context`** (Windows). They are **not supported on `epoll_context`**: `epoll(7)` cannot wait on regular files, so `epoll_context::scheduler::make_io_object` throws `std::system_error` with `std::errc::operation_not_permitted` when handed a regular file or directory descriptor — i.e. `open()`ing (or constructing with a path) a `stream_file`/`random_access_file` on an `epoll_context` scheduler throws. Default-constructing the closed wrapper is fine.

## Synopsis

```cpp
namespace coio {
    template<io_scheduler IoScheduler>
    class stream_file /* : file-base */ {
    public:
        using native_handle_type = /* int on Linux, void* (HANDLE) on Windows */;
        using scheduler_type = IoScheduler;
        using enum /* open_mode */;     // read_only, write_only, read_write, append,
                                        // create, exclusive, truncate, sync_all_on_write
        using enum /* seek_whence */;   // seek_set, seek_cur, seek_end

        explicit stream_file(scheduler_type scheduler) noexcept;
        stream_file(scheduler_type scheduler, native_handle_type handle);
        stream_file(IoScheduler scheduler, zstring_view path, /* open_mode */ mode);
        stream_file(stream_file&&) = default;                 // move-only

        auto open(zstring_view path, /* open_mode */ mode) -> void;
        auto close() -> void;
        auto cancel() -> void;
        auto is_open() const noexcept -> bool;
        explicit operator bool() const noexcept;
        auto native_handle() const noexcept -> native_handle_type;
        auto get_io_scheduler() const noexcept -> scheduler_type;

        auto size() const -> std::size_t;
        auto resize(std::size_t new_size) -> void;
        auto seek(std::size_t offset, /* seek_whence */ whence) -> std::size_t;
        auto sync_all() -> void;
        auto sync_data() -> void;

        auto read_some(std::span<std::byte> buffer) -> std::size_t;
        auto write_some(std::span<const std::byte> buffer) -> std::size_t;
        auto async_read_some(std::span<std::byte> buffer);        // sender of std::size_t
        auto async_write_some(std::span<const std::byte> buffer); // sender of std::size_t
    };

    template<io_scheduler IoScheduler>
    class random_access_file /* : file-base */ {
    public:
        // same constructors, open/close/cancel/is_open/native_handle/get_io_scheduler,
        // size/resize/sync_all/sync_data as stream_file, but no seek; data path:
        auto read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t;
        auto write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t;
        auto async_read_some_at(std::size_t offset, std::span<std::byte> buffer);        // sender of std::size_t
        auto async_write_some_at(std::size_t offset, std::span<const std::byte> buffer); // sender of std::size_t
    };
}
```

## API Reference

### Open modes

The open mode is a flag enum; combine flags with `|`. The enumerators are re-exported into both file classes (`stream_file::read_only`, `random_access_file::create`, ...).

| Flag | Meaning |
|------|---------|
| `read_only` | Open for reading only |
| `write_only` | Open for writing only |
| `read_write` | Open for both reading and writing |
| `append` | Writes occur at end of file |
| `create` | Create the file if it doesn't exist |
| `exclusive` | Ensure creation of a new file (fails if the file exists) |
| `truncate` | Truncate an existing file to zero length |
| `sync_all_on_write` | Synchronize all writes to disk immediately |

### Common members (both classes)

#### `explicit stream_file(scheduler_type scheduler) noexcept`
Constructs a closed file bound to `scheduler`.

#### `stream_file(scheduler_type scheduler, native_handle_type handle)`
Adopts an already-open native handle. The file object takes ownership.
On Windows the handle must have been opened with `FILE_FLAG_OVERLAPPED`.

#### `stream_file(IoScheduler scheduler, zstring_view path, mode)`
Constructs and opens; equivalent to default construction followed by `open(path, mode)`. Throws `std::system_error` on failure.

#### `open(zstring_view path, mode) -> void`
Opens the file at `path`. Throws `std::system_error` with `coio::error::already_open` if the file is already open, or with an OS error code on failure (including the `epoll_context` regular-file rejection described above).

#### `close() -> void`
Cancels any outstanding asynchronous operations on the file (they complete with `set_stopped()`), then releases the handle. Throws `std::system_error` on OS failure. The destructor closes implicitly. See [close semantics per backend](model.md#close).

#### `cancel() -> void`
Cancels outstanding asynchronous read/write operations on this file; they complete with `set_stopped()`.

#### `is_open() const noexcept -> bool` / `explicit operator bool()`
Whether the file holds a valid handle.

#### `native_handle() const noexcept` / `get_io_scheduler() const noexcept`
Access the underlying handle / the bound scheduler.

#### `size() const -> std::size_t`
Current file size in bytes. Throws `std::system_error` on failure.

#### `resize(std::size_t new_size) -> void`
Sets the file size (truncates or extends). Throws `std::system_error` on failure.

#### `sync_all() -> void` / `sync_data() -> void`
Block until modified data (and, for `sync_all`, metadata) reaches the storage device. Throw `std::system_error` on failure.

### `stream_file` data path

#### `read_some(std::span<std::byte> buffer) -> std::size_t`
Synchronously reads up to `buffer.size()` bytes at the current position and advances the position by the amount read. Returns the number of bytes read (at least 1 for a non-empty buffer). Throws `std::system_error`; end-of-file is reported as `coio::error::eof`. An empty buffer returns 0 immediately.

#### `write_some(std::span<const std::byte> buffer) -> std::size_t`
Synchronously writes up to `buffer.size()` bytes at the current position and advances the position. Returns the number of bytes written. Throws `std::system_error` on failure.

#### `async_read_some(std::span<std::byte> buffer)`
Returns a sender that reads up to `buffer.size()` bytes at the current stream position and advances it. Completes with `set_value(std::size_t)`, or `set_error(std::error_code)` — end-of-file arrives as `coio::error::eof` — or `set_stopped()` on cancellation.

#### `async_write_some(std::span<const std::byte> buffer)`
Returns a sender that writes up to `buffer.size()` bytes at the current stream position and advances it. Completes with `set_value(std::size_t)` / `set_error(std::error_code)` / `set_stopped()`.

#### `seek(std::size_t offset, whence) -> std::size_t`
Moves the stream position; returns the new absolute position. `whence` is one of `seek_set` (from beginning), `seek_cur` (from current), `seek_end` (from end). Throws `std::system_error` on failure.

!!! note "IOCP: the stream offset is consumed when the sender is built"
    On `iocp_context` the stream position lives in the io object, and `async_read_some`/`async_write_some` reserve their offset range **at sender-creation time** (Asio-style). A sender that is created but never started still advances the position. Create-then-start senders one at a time, in order (which the [outstanding-operation limits](model.md#outstanding-operation-limits) require anyway).

### `random_access_file` data path

`random_access_file` has no position and no `seek`; the buffer must stay valid until completion, and concurrent operations at different offsets are still subject to the one-read + one-write outstanding limit.

#### `read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t`
Synchronous positional read; does not modify any file position. EOF throws `coio::error::eof`.

#### `write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t`
Synchronous positional write; does not modify any file position.

#### `async_read_some_at(std::size_t offset, std::span<std::byte> buffer)`
Sender of `std::size_t`; completes with `coio::error::eof` via `set_error` if the read hits end-of-file (0 bytes into a non-empty buffer).

#### `async_write_some_at(std::size_t offset, std::span<const std::byte> buffer)`
Sender of `std::size_t`.

### Thread safety

File objects are **not thread-safe**: do not call member functions concurrently on the same object. The [outstanding-operation limits and lifetime rules](model.md#outstanding-operation-limits) of the I/O object model apply.

## Example

Positional file copy with `random_access_file` (adapted from `examples/cp.cpp`):

```cpp
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/file.h>

#if COIO_OS_LINUX
#include <coio/asyncio/uring_context.h>     // epoll_context does not support files
using io_context = coio::uring_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using file = coio::random_access_file<io_context::scheduler>;

auto copy_file(coio::zstring_view src, coio::zstring_view dst) -> io_context::task<> {
    auto sched = co_await coio::read_scheduler();
    file src_file{sched, src, file::read_only};
    file dst_file{sched, dst, file::write_only | file::create | file::truncate};

    const std::size_t total = src_file.size();
    dst_file.resize(total);

    try {
        std::byte buffer[1024];
        std::size_t offset = 0;
        while (offset < total) {
            const auto n = co_await src_file.async_read_some_at(
                offset, coio::as_writable_bytes(buffer));
            co_await coio::async_write_at(dst_file, offset, coio::as_bytes(buffer, n));
            offset += n;
        }
    }
    catch (const std::system_error& e) {
        if (e.code() != coio::error::eof) throw;
    }
    dst_file.sync_all();
}

auto main(int argc, char** argv) -> int {
    if (argc != 3) return 1;
    io_context context;
    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(context.get_scheduler(), copy_file(argv[1], argv[2])),
        [&]() -> coio::task<> { context.run(); co_return; }()
    ));
}
```

## See also

- [I/O object model](model.md) — lifetime, cancellation, `close()` semantics
- [Pipes](pipes.md) — the pipe wrappers share the stream-file surface
- [I/O algorithms](algorithms.md) — `async_read_at` / `async_write_at` complete-transfer loops
- [io_uring context](../execution/uring.md), [IOCP context](../execution/iocp.md)
- [Error handling](../error-handling.md)
