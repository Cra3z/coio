# File I/O Guide

File I/O requires a completion-based I/O context: `uring_context` on Linux or `iocp_context` on Windows. `epoll_context` does **not** support file I/O.

## Choosing a File Type

| Type | Model | When to Use |
|------|-------|-------------|
| `stream_file` | Sequential read/write | Pipes, reading from start to end, streaming |
| `random_access_file` | Positional read/write | Copying, database files, writing at arbitrary offsets |

## Opening Files

Files are opened by providing a path and open mode flags:

```cpp
#include <coio/asyncio/file.h>

// Read-only, file must exist
auto file = coio::stream_file{sched, "/tmp/data.txt",
    coio::stream_file::read_only};

// Write-only, create or truncate
auto file = coio::stream_file{sched, "/tmp/output.txt",
    coio::stream_file::write_only
    | coio::stream_file::create
    | coio::stream_file::truncate};

// Read + write, create if not exists
auto file = coio::random_access_file{sched, "/tmp/data.bin",
    coio::random_access_file::read_write
    | coio::random_access_file::create};
```

### Open Mode Flags

| Flag | Meaning |
|------|---------|
| `read_only` | Open for reading |
| `write_only` | Open for writing |
| `read_write` | Open for both |
| `append` | Seek to end before each write |
| `create` | Create if not exists |
| `exclusive` | Fail if exists (with `create`) |
| `truncate` | Truncate to zero length |
| `sync_all_on_write` | `fsync` after each write |

## Reading and Writing

### Low-level: async_read_some / async_write_some

These read/write at least one byte (up to the buffer size):

```cpp
std::byte buf[4096];
auto n = co_await file.async_read_some(coio::as_writable_bytes(buf));
// n bytes were read
```

### High-level: coio::async_read / coio::async_write

These transfer exactly the requested number of bytes, looping internally:

```cpp
std::byte buf[4096];
auto n = co_await coio::async_read(file, coio::as_writable_bytes(buf));
// exactly 4096 bytes were read (or error/eof)
```

## Reading a File (cat)

```cpp
#include <coio/asyncio/uring_context.h>
#include <coio/asyncio/file.h>
#include <coio/asyncio/io.h>

auto cat_file(io_context& ctx, coio::zstring_view path) -> coio::task<> {
    coio::stream_file file{ctx.get_scheduler(), path,
        coio::stream_file::read_only};
    std::println("Size: {} bytes", file.size());

    char buf[4096];
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
}
```

## Copying a File (cp)

Use `random_access_file` for copying:

```cpp
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
    std::println("Copied {} bytes", total);
}
```

## File Management

| Operation | Description |
|-----------|-------------|
| `file.size()` | Returns the file size in bytes. |
| `file.resize(n)` | Sets the file size to `n` bytes. |
| `file.seek(offset, whence)` | Moves the file position. Returns new position. |
| `file.sync_all()` | Flushes data and metadata to disk. |
| `file.sync_data()` | Flushes data to disk. |
| `file.close()` | Closes the file. |

## Complete Example

```cpp
#if COIO_OS_LINUX
#include <coio/asyncio/uring_context.h>
using io_context = coio::uring_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

int main(int argc, char** argv) {
    if (argc != 2) {
        std::println("Usage: {} <file>", argv[0]);
        return 1;
    }
    io_context ctx;

    coio::this_thread::sync_wait(coio::when_all(
        cat_file(ctx, argv[1]),
        [&]() -> coio::task<> { ctx.run(); co_return; }()
    ));
}
```

## Platform Notes

| Platform | Context | File I/O | Notes |
|----------|---------|----------|-------|
| Linux | `uring_context` | Yes | Requires liburing |
| Linux | `epoll_context` | No | Use uring_context |
| Windows | `iocp_context` | Yes | Built-in support |
