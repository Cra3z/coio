# Pipes

An anonymous, unidirectional byte pipe: `make_pipe` creates a connected `pipe_reader`/`pipe_writer` pair whose ends behave like stream files — synchronous `read_some`/`write_some` and sender-returning `async_read_some`/`async_write_some`. The two ends may even be bound to different execution contexts.

Header: `#include <coio/asyncio/pipe.h>`

## Overview

- `pipe_reader<IoScheduler>` — the read end: `read_some` / `async_read_some`.
- `pipe_writer<IoScheduler>` — the write end: `write_some` / `async_write_some`.
- `coio::make_pipe` — factory; creates the OS pipe and wraps both ends, or adopts existing native handles.

Both classes derive from the same stream-file base as [`stream_file`](files.md), so they also provide the common members `close()`, `cancel()`, `is_open()`, `explicit operator bool()`, `native_handle()`, `get_io_scheduler()`, and the `(scheduler)` / `(scheduler, native_handle)` constructors. They are move-only and satisfy the [stream device concepts](algorithms.md#device-concepts), so the free algorithms compose with them — `coio::read`/`coio::async_read`/`coio::async_read_until` on the reader, `coio::write`/`coio::async_write` on the writer.

## Synopsis

```cpp
namespace coio {
    template<io_scheduler IoScheduler>
    class pipe_reader /* : stream-file base */ {
    public:
        explicit pipe_reader(IoScheduler scheduler) noexcept;
        pipe_reader(IoScheduler scheduler, /* native handle */ handle);

        auto read_some(std::span<std::byte> buffer) -> std::size_t;
        auto async_read_some(std::span<std::byte> buffer);   // sender of std::size_t
        // + close(), cancel(), is_open(), native_handle(), get_io_scheduler(), ...
    };

    template<io_scheduler IoScheduler>
    class pipe_writer /* : stream-file base */ {
    public:
        explicit pipe_writer(IoScheduler scheduler) noexcept;
        pipe_writer(IoScheduler scheduler, /* native handle */ handle);

        auto write_some(std::span<const std::byte> buffer) -> std::size_t;
        auto async_write_some(std::span<const std::byte> buffer);  // sender of std::size_t
        // + close(), cancel(), is_open(), native_handle(), get_io_scheduler(), ...
    };

    inline constexpr /* unspecified */ make_pipe;
    // make_pipe(sched)                          -> std::pair<pipe_reader<S>, pipe_writer<S>>
    // make_pipe(reader_sched, writer_sched)     -> std::pair<pipe_reader<S1>, pipe_writer<S2>>
    // make_pipe(sched, reader_h, writer_h)      -> adopt existing handles, one scheduler
    // make_pipe(sched1, reader_h, sched2, writer_h) -> adopt existing handles, two schedulers
}
```

The native handle type is `int` on Linux and `HANDLE` (`void*`) on Windows.

## API Reference

### `make_pipe`

```cpp
make_pipe(IoScheduler sched)
    -> std::pair<pipe_reader<IoScheduler>, pipe_writer<IoScheduler>>;

make_pipe(ReaderIoScheduler reader_sched, WriterIoScheduler writer_sched)
    -> std::pair<pipe_reader<ReaderIoScheduler>, pipe_writer<WriterIoScheduler>>;

make_pipe(IoScheduler sched,
          file_native_handle_type reader_handle,
          file_native_handle_type writer_handle)
    -> std::pair<pipe_reader<IoScheduler>, pipe_writer<IoScheduler>>;

make_pipe(ReaderIoScheduler sched1, file_native_handle_type reader_handle,
          WriterIoScheduler sched2, file_native_handle_type writer_handle)
    -> std::pair<pipe_reader<ReaderIoScheduler>, pipe_writer<WriterIoScheduler>>;
```

The first two overloads create a fresh OS pipe; the last two adopt handles you already own (ownership transfers to the wrappers). The dual-scheduler forms let the read end and the write end complete on different execution contexts (e.g. two `epoll_context`s run by different threads).

Throws `std::system_error` if pipe creation or io-object registration fails.

### `pipe_reader`

#### `read_some(std::span<std::byte> buffer) -> std::size_t`
Blocking read of up to `buffer.size()` bytes. Returns the number of bytes read (≥ 1 for a non-empty buffer); returns 0 for an empty buffer. Throws `std::system_error`; when the write end has been closed and the pipe is drained, throws with `coio::error::eof`.

#### `async_read_some(std::span<std::byte> buffer)`
Returns a sender completing with `set_value(std::size_t)` once at least one byte is available, `set_error(std::error_code)` on failure — **writer closed** is reported as `coio::error::eof` — or `set_stopped()` on cancellation.

### `pipe_writer`

#### `write_some(std::span<const std::byte> buffer) -> std::size_t`
Blocking write of up to `buffer.size()` bytes; returns the number written. Throws `std::system_error` on failure. On Windows, writing after the reader has closed fails with a broken-pipe error; for POSIX behavior see the broken-pipe contract below.

#### `async_write_some(std::span<const std::byte> buffer)`
Returns a sender completing with `set_value(std::size_t)` / `set_error(std::error_code)` / `set_stopped()`. The broken-pipe contract below applies here too.

### Contracts

- **EOF**: closing (or destroying) the `pipe_writer` causes reads on the `pipe_reader` to report `coio::error::eof` once buffered data is drained. On Windows this is surfaced by the OS as `ERROR_BROKEN_PIPE`, which coio maps to the same `coio::error::eof`.
- **Broken pipe**: on Windows, a write after the reader has closed fails with an error (`std::system_error` from `write_some`, `set_error` from `async_write_some`). On POSIX platforms, writing to a pipe whose read end has closed raises `SIGPIPE`, whose default disposition terminates the process; only when `SIGPIPE` is ignored or blocked does the write fail with `EPIPE`. POSIX applications that use pipes should ignore `SIGPIPE` (e.g. `std::signal(SIGPIPE, SIG_IGN)`) so a broken pipe surfaces as an error code.
- **Lifetime / outstanding operations**: each end is an I/O object; the [model-wide rules](model.md#lifetime) apply — at most one outstanding read on the reader and one outstanding write on the writer, senders must be started before the end is closed/destroyed, and each end must outlive its operations.
- **Thread safety**: the ends are independent objects; using the reader from one thread and the writer from another is fine. A single end is not thread-safe.

### Platform notes

| Platform | Implementation |
|----------|----------------|
| Linux | `pipe2(O_CLOEXEC)`. Both `epoll_context` and `uring_context` can host pipe ends; on `epoll_context` the adopted fds are switched to `O_NONBLOCK`. |
| Windows | A uniquely-named `\\.\pipe\coio_<pid>_<n>` named-pipe pair created with `FILE_FLAG_OVERLAPPED`, byte mode, 4096-byte buffers; the reader end is the server side. |

!!! note
    Unlike regular files, pipes **are** supported on `epoll_context` (they are pollable). All three I/O contexts can host pipe ends.

## Example

Adapted from `examples/pipe.cpp` — one task writes lines, another echoes them until EOF:

```cpp
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/pipe.h>
#include <iostream>

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

auto reader_task(coio::pipe_reader<io_context::scheduler> r) -> coio::task<> {
    try {
        char buffer[128];
        while (true) {
            auto n = co_await r.async_read_some(coio::as_writable_bytes(buffer));
            std::clog << std::string_view{buffer, n};
        }
    }
    catch (const std::system_error& e) {
        if (e.code() != coio::error::eof) throw;   // writer closed: normal shutdown
    }
}

auto writer_task(coio::pipe_writer<io_context::scheduler> w) -> coio::task<> {
    for (std::string_view msg : {"hello ", "from ", "a ", "pipe\n"}) {
        std::size_t written = 0;
        while (written < msg.size()) {
            written += co_await w.async_write_some(coio::as_bytes(msg).subspan(written));
        }
    }
}   // ~pipe_writer closes the write end -> reader sees EOF

auto main() -> int {
    io_context context;
    auto [reader, writer] = coio::make_pipe(context.get_scheduler());

    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), reader_task(std::move(reader)));
    scope.spawn_on(context.get_scheduler(), writer_task(std::move(writer)));

    context.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## See also

- [I/O object model](model.md) — lifetime, cancellation, `close()` semantics
- [Files](files.md) — the shared stream-file surface
- [I/O algorithms](algorithms.md) — `async_write` for complete transfers, `async_read_until`
- [epoll context](../execution/epoll.md), [IOCP context](../execution/iocp.md)
