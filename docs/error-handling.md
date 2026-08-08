# Error Handling

coio reports failures through the standard `<system_error>` machinery: synchronous operations throw `std::system_error`, asynchronous operations complete with `set_error(std::error_code)`, and coroutine bodies propagate exceptions as `set_error(std::exception_ptr)`. Library-specific conditions (such as end-of-stream) are identified by the `coio::error::misc_errc` enum and its dedicated error category.

Header: `#include <coio/detail/error.h>` (pulled in automatically by `<coio/asyncio/io.h>`, `<coio/asyncio/file.h>`, and `<coio/net/socket.h>`)

## Overview

There are three completion channels, and errors use exactly one of them:

| Channel | Meaning |
|---------|---------|
| `set_value(...)` | success |
| `set_error(e)` | failure — `e` is `std::error_code` for I/O operations, `std::exception_ptr` for exceptions escaping a `task` body |
| `set_stopped()` | cancellation — **not** an error |

Cancellation is deliberately kept out of the error channel: a stop request never overwrites a real result, and `set_stopped` is delivered only when the cancellation path wins (see the conventions in [Concepts](concepts.md)).

## Synopsis

```cpp
namespace coio::error {
    enum misc_errc : int {
        eof = 1,        // end of stream / file
        already_open,   // open() on an already-open file/socket
        not_found,      // read_until: delimiter not found before max_size()
        overflow        // size/buffer limit exceeded (reserved)
    };

    [[nodiscard]] auto misc_category() noexcept -> const std::error_category&;
    [[nodiscard]] auto make_error_code(misc_errc e) noexcept -> std::error_code;

    [[nodiscard]] auto gai_category() noexcept -> const std::error_category&; // getaddrinfo errors
}

template<> struct std::is_error_code_enum<coio::error::misc_errc> : std::true_type {};
```

## API Reference

### error::misc_errc

Library-specific error codes:

| Value | Meaning |
|-------|---------|
| `eof` | End of stream. Reported by `read_some`/`async_read_some` on streams (sockets, pipes, files) when the peer closed / the end of the file was reached and the request was non-empty. |
| `already_open` | `open()` was called on a file or socket object that is already open. |
| `not_found` | Reported by `read_until`/`async_read_until` when the delimiter is not found and the dynamic buffer cannot grow further (`size() == max_size()`), matching asio's `error::not_found`. The synchronous form throws `std::system_error`; the asynchronous form completes with this code and size 0. Data read so far stays committed in the buffer. |
| `overflow` | A size or buffer limit was exceeded. Declared for library use; not reported by any built-in operation at present. |

Because `std::is_error_code_enum` is specialized, enumerators convert implicitly to `std::error_code` and compare directly:

```cpp
if (ec == coio::error::eof) { /* clean end of stream */ }
```

### error::misc_category

```cpp
[[nodiscard]] auto misc_category() noexcept -> const std::error_category&;
```

The singleton category for `misc_errc` codes. `name()` returns `"coio.error.misc"`; `message()` returns `"end of file"`, `"already open"`, `"not found"`, or `"overflow"`.

### error::make_error_code

```cpp
[[nodiscard]] auto make_error_code(misc_errc e) noexcept -> std::error_code;
```

Builds `std::error_code{e, misc_category()}`; found by ADL, which is what enables the implicit `misc_errc` → `error_code` conversion.

### error::gai_category

```cpp
[[nodiscard]] auto gai_category() noexcept -> const std::error_category&;
```

Category for `getaddrinfo` failure codes. When name resolution fails, the [resolver](net/resolver.md)'s synchronous `resolve` throws `std::system_error` with this category at the call; `async_resolve` delivers the same exception through the sender's error channel (`set_error(std::exception_ptr)`).

Errors originating from the operating system use the standard `std::system_category()`.

## How errors surface

### Synchronous operations

Synchronous member functions (`open`, `bind`, `connect`, `listen`, `read_some`, `write_some`, option setters, ...) **throw `std::system_error`** on failure. The embedded code is an OS error (`std::system_category()`) or a `coio::error` code — e.g. `open()` on an open object throws `error::already_open`, and a synchronous `read_some` at end-of-stream throws `error::eof`.

### Asynchronous operations

Senders for async I/O (`async_read_some`, `async_accept`, `async_wait`, `signal_wait`, ...) **never throw** for operational failures; they complete with `set_error(std::error_code)`. Their completion signatures name `std::error_code` as the error type.

### Inside coroutines

`co_await`-ing a sender inside a `coio::task` converts an error completion into an exception at the `co_await` expression: `set_error(std::error_code)` is thrown as `std::system_error`, and `set_error(std::exception_ptr)` is rethrown as-is. So inside a task both sync and async failures are consumed with ordinary `try`/`catch`:

```cpp
try {
    auto n = co_await sock.async_read_some(buffer);
} catch (const std::system_error& e) {
    if (e.code() != coio::error::eof) throw;  // eof is expected here
}
```

An exception that escapes a task body is captured and delivered to the task's receiver as `set_error(std::exception_ptr)`.

### At the blocking boundary

`coio::this_thread::sync_wait` translates an error completion into an exception on the waiting thread: an `std::exception_ptr` is rethrown, an `std::error_code` is thrown as `std::system_error`, any other error object is thrown as-is. `set_stopped` becomes an empty `std::optional`, not an exception.

### Fire-and-forget work

`async_scope::spawn` wraps the spawned sender so that an **error completion calls `std::terminate()`** — background work has nowhere to report errors, so handle them inside the spawned sender (see [async_scope](utils/async-scope.md)).

## EOF conventions

End-of-stream is an *error-channel* condition, `coio::error::eof`, never a zero-byte success:

- Stream sockets: `read_some`/`async_read_some` report `eof` when the peer performs an orderly shutdown ([sockets](net/sockets.md)).
- Files and pipes: reads at end-of-file (or on a broken pipe on Windows) report `eof` ([files](io/files.md), [pipes](io/pipes.md)).
- Composed reads (`async_read`, `read_until`, ...) forward `eof` from the underlying device ([I/O algorithms](io/algorithms.md)).

A zero-length read *request* on a stream completes with `set_value(0)`; `eof` is only reported for non-empty requests. (On datagram sockets zero-length operations are real — see [sockets](net/sockets.md).)

## Example

A `cat`-style read loop distinguishing clean EOF from real failures (from `examples/cat.cpp` / `examples/cp.cpp`):

```cpp
auto cat(auto& file) -> coio::task<> {
    std::array<std::byte, 4096> buf;
    try {
        while (true) {
            const std::size_t n = co_await file.async_read_some(buf);
            write_stdout(std::span{buf}.first(n));
        }
    }
    catch (const std::system_error& e) {
        if (e.code() != coio::error::eof) throw; // eof terminates the loop cleanly
    }
}
```

## See also

- [Concepts](concepts.md) — completion channels and the cancellation contract
- [I/O model](io/model.md) — sync vs async operation shapes
- [Sockets](net/sockets.md) — socket-specific EOF behavior
- [Waiting & Algorithms](utils/algorithms.md) — `sync_wait`'s throw behavior
- [Thread safety](thread-safety.md)
