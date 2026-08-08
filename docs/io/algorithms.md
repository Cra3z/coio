# I/O Algorithms

`<coio/asyncio/io.h>` provides the device and buffer **concepts** that describe coio's I/O wrappers, and free **algorithms** built on top of them: complete-transfer loops (`read`/`write`/`async_read`/`async_write` and their `_at` positional forms) that repeat `*_some` calls until a whole buffer is transferred, delimiter reads (`read_until`/`async_read_until`), and the `as_bytes`/`as_writable_bytes` span helpers.

Header: `#include <coio/asyncio/io.h>`

## Overview

The `*_some` members of sockets, files and pipes are allowed to transfer *fewer* bytes than requested. The algorithms on this page hide that:

| Algorithm | Devices | Guarantees |
|-----------|---------|------------|
| `read` / `write` | sync stream devices | loops over `read_some`/`write_some` until the buffer is complete; throws on error |
| `read_at` / `write_at` | sync random-access devices | same, advancing an offset |
| `async_read` / `async_write` | async stream devices | sender; loops over `async_read_some`/`async_write_some` |
| `async_read_at` / `async_write_at` | async random-access devices | sender; loops over the `_at` forms |
| `read_until` / `async_read_until` | (async) input stream device + **dynamic buffer** | reads until a `char` or `std::string_view` delimiter appears |
| `as_bytes` / `as_writable_bytes` | — | build `std::span<const std::byte>` / `std::span<std::byte>` from anything `std::span` can view |

All of them accept single contiguous spans (or a dynamic buffer); there is no scatter-gather buffer sequence type.

!!! note "Error reporting differs between the sync and async families"
    The synchronous algorithms **throw `std::system_error`** (propagated from the device's `*_some` members). The asynchronous algorithms **never complete with `set_error` or `set_stopped`**: they complete with `set_value(std::error_code, std::size_t)`, where the error code carries the failure (cancellation arrives as `std::errc::operation_canceled`) and the size is the byte count transferred before the failure. Check the error code, or adapt the sender if you prefer exceptions.

## Synopsis

```cpp
namespace coio {
    // --- device concepts -----------------------------------------------------
    template<typename T> concept input_stream_device;          // t.read_some(span<byte>) -> integral
    template<typename T> concept output_stream_device;         // t.write_some(span<const byte>) -> integral
    template<typename T> concept input_random_access_device;   // t.read_some_at(size_t, span<byte>) -> integral
    template<typename T> concept output_random_access_device;  // t.write_some_at(size_t, span<const byte>) -> integral
    template<typename T> concept stream_device;                // input + output
    template<typename T> concept random_access_device;         // input + output

    template<typename T> concept async_input_stream_device;          // t.async_read_some(...) -> sender
    template<typename T> concept async_output_stream_device;         // t.async_write_some(...) -> sender
    template<typename T> concept async_input_random_access_device;   // t.async_read_some_at(...) -> sender
    template<typename T> concept async_output_random_access_device;  // t.async_write_some_at(...) -> sender
    template<typename T> concept async_stream_device;
    template<typename T> concept async_random_access_device;

    template<typename T> concept dynamic_buffer;   // size/capacity/max_size/data/prepare/commit/consume

    // --- synchronous algorithms (throw std::system_error) --------------------
    std::size_t read(input_stream_device auto& device, std::span<std::byte> buffer);
    std::size_t read(input_stream_device auto& device, dynamic_buffer auto& dyn, std::size_t total);
    std::size_t write(output_stream_device auto& device, std::span<const std::byte> buffer);
    std::size_t write(output_stream_device auto& device, dynamic_buffer auto& dyn);
    std::size_t read_at(input_random_access_device auto& device, std::size_t offset, std::span<std::byte> buffer);
    std::size_t read_at(input_random_access_device auto& device, std::size_t offset, dynamic_buffer auto& dyn, std::size_t total);
    std::size_t write_at(output_random_access_device auto& device, std::size_t offset, std::span<const std::byte> buffer);
    std::size_t write_at(output_random_access_device auto& device, std::size_t offset, dynamic_buffer auto& dyn, std::size_t total);
    std::size_t read_until(input_stream_device auto& device, dynamic_buffer auto& dyn, char delim);
    std::size_t read_until(input_stream_device auto& device, dynamic_buffer auto& dyn, std::string_view delim);

    // --- asynchronous algorithms (senders of (std::error_code, std::size_t)) -
    auto async_read(async_input_stream_device auto& device, std::span<std::byte> buffer);
    auto async_read(async_input_stream_device auto& device, dynamic_buffer auto& dyn, std::size_t total);
    auto async_write(async_output_stream_device auto& device, std::span<const std::byte> buffer);
    auto async_write(async_output_stream_device auto& device, dynamic_buffer auto& dyn);
    auto async_read_at(async_input_random_access_device auto& device, std::size_t offset, std::span<std::byte> buffer);
    auto async_read_at(async_input_random_access_device auto& device, std::size_t offset, dynamic_buffer auto& dyn, std::size_t total);
    auto async_write_at(async_output_random_access_device auto& device, std::size_t offset, std::span<const std::byte> buffer);
    auto async_write_at(async_output_random_access_device auto& device, std::size_t offset, dynamic_buffer auto& dyn);
    auto async_read_until(async_input_stream_device auto& device, dynamic_buffer auto& dyn, char delim);
    auto async_read_until(async_input_stream_device auto& device, dynamic_buffer auto& dyn, std::string_view delim);

    // --- span helpers --------------------------------------------------------
    auto as_bytes(auto&&... args) -> std::span<const std::byte>;   // std::as_bytes(std::span(args...))
    auto as_writable_bytes(auto&&... args) -> std::span<std::byte>; // std::as_writable_bytes(std::span(args...))
}
```

(These entities are customization-point-style function objects; they are shown as functions for readability.)

## API Reference

### Device concepts

A type models a device concept purely structurally:

- `input_stream_device`: `t.read_some(std::span<std::byte>) -> std::integral`
- `output_stream_device`: `t.write_some(std::span<const std::byte>) -> std::integral`
- `input_random_access_device` / `output_random_access_device`: the `read_some_at`/`write_some_at` forms taking a leading `std::size_t` offset
- `async_input_stream_device` etc.: the `async_*` spelling must return an `execution::sender`
- `stream_device`, `random_access_device`, `async_stream_device`, `async_random_access_device`: input + output combinations

`basic_stream_socket` and pipes model the stream concepts; `stream_file` models the stream concepts; `random_access_file` models the random-access concepts.

### `dynamic_buffer`

A growable byte FIFO with Asio-style `prepare`/`commit`/`consume`:

```cpp
template<typename T>
concept dynamic_buffer = requires (T t, const T& ct, std::size_t n) {
    { ct.size() }     -> std::integral;
    { ct.capacity() } -> std::integral;
    { ct.max_size() } -> std::integral;
    { ct.data() }     -> std::convertible_to<std::span<const std::byte>>;
    { t.prepare(n) }  -> std::convertible_to<std::span<std::byte>>;
    t.commit(n);
    t.consume(n);
};
```

`data()` views the readable region; `prepare(n)` returns writable space for at least `n` bytes (may reallocate), `commit(n)` moves prepared bytes into the readable region, `consume(n)` discards from the front. coio ships two models: **`coio::flat_buffer`** (`<coio/utils/flat_buffer.h>`) and **`coio::streambuf`** (`<coio/utils/streambuf.h>`, also a `std::streambuf`). See [Buffers](../utils/buffers.md).

### `read` / `write` (synchronous)

#### `read(device, std::span<std::byte> buffer) -> std::size_t`
Calls `device.read_some` repeatedly until `buffer` is completely filled. Returns `buffer.size()`. Any error (including EOF before the buffer is full, for devices that signal EOF by throwing) propagates as `std::system_error`; bytes read before the throw are lost to the caller's accounting.

#### `read(device, dynamic_buffer auto& dyn, std::size_t total) -> std::size_t`
Fills `dyn.prepare(total)` completely, commits it, returns `total`.

#### `write(device, std::span<const std::byte> buffer) -> std::size_t`
Calls `device.write_some` until all of `buffer` is written; returns `buffer.size()`.

#### `write(device, dynamic_buffer auto& dyn) -> std::size_t`
Writes the buffer's entire readable region (`dyn.data()`), then `consume`s it. Returns the byte count.

#### `read_at` / `write_at`
Positional variants for random-access devices; the offset advances internally as partial transfers complete. Signatures as in the synopsis.

Empty buffers: all complete-transfer algorithms return 0 immediately for an empty span.

### `async_read` / `async_write` (asynchronous)

Each returns a lazy sender that repeatedly starts the device's corresponding `async_*_some` operation until the transfer is complete, an error occurs, or it is stopped.

**Completion**: exactly one signature, `set_value(std::error_code ec, std::size_t n)`.

- Success: `ec` is empty (`!ec`), `n` is the full requested size.
- Failure: `ec` is the device error (`coio::error::eof` if a stream ended early), `n` is the number of bytes transferred before the failure.
- Cancellation of the underlying operation is folded into `ec == std::errc::operation_canceled`; the sender does not complete with `set_stopped`.

`async_read(device, dyn, total)` calls `dyn.prepare(total)` **when the sender is created** and commits the transferred bytes on completion. `async_write(device, dyn)` snapshots `dyn.data()` at creation and `consume`s the written bytes on completion. The device and the dynamic buffer are captured by reference and must outlive the operation.

The `async_read_at`/`async_write_at` forms mirror these for `async_*_some_at` devices, threading the offset through partial transfers.

To turn the `(ec, n)` completion into an exception/value split, adapt it, e.g.:

```cpp
auto as_throwing = coio::execution::let_value(
    [](std::error_code ec, std::size_t n) {
        coio::async_result<coio::execution::set_value_t(std::size_t),
                           coio::execution::set_error_t(std::error_code)> r;
        if (ec) {
            if (ec == std::errc::operation_canceled) r.set_stopped();
            else r.set_error(ec);
        }
        else r.set_value(n);
        return r;
    });

std::size_t n = co_await (coio::async_write(socket, coio::as_bytes(data)) | as_throwing);
```

### `read_until` / `async_read_until`

Read into a dynamic buffer until it contains a delimiter.

#### `read_until(device, dyn, char delim) -> std::size_t`
#### `read_until(device, dyn, std::string_view delim) -> std::size_t`
Searches the buffer's existing readable data first, then reads more (committing as it goes) until the delimiter is found. Returns the number of readable bytes **up to and including the delimiter**, counted from the front of the buffer's readable region. Returns 0 if the delimiter is an empty `std::string_view`. If the delimiter is still missing once the buffer cannot grow any further (`dyn.size() == dyn.max_size()`), throws `std::system_error` with `coio::error::not_found` (matching asio); the data read so far stays committed in the buffer. Other errors from `read_some` propagate as exceptions. Each iteration reads at most `min(max(512, capacity - size), min(65536, max_size - size))` bytes, so `prepare` never requests space beyond `max_size()`.

#### `async_read_until(device, dyn, char delim)` / `async_read_until(device, dyn, std::string_view delim)`
Sender completing with `set_value(std::error_code, std::size_t)`:

- Delimiter found: empty error code, position one-past-the-delimiter (as above).
- Delimiter not found and the buffer cannot grow (`dyn.size() == dyn.max_size()`): `coio::error::not_found`, size 0 (matching asio). Data read so far remains committed in the buffer.
- Device error (including `coio::error::eof` from stream wrappers when the peer closes before a delimiter arrives): that error code, size 0. Data read so far remains committed in the buffer.
- Cancellation: `std::errc::operation_canceled`, size 0.
- Empty `std::string_view` delimiter: completes immediately with (no error, 0).

The async form grows the buffer with the same `min(max(512, capacity - size), min(65536, max_size - size))` per-step read size as the synchronous form, so `prepare` never requests space beyond `max_size()`.

The buffer may contain data **beyond** the delimiter after completion; `consume` only what you parsed.

!!! warning
    `device` and the buffer are captured by pointer/reference; both must outlive the operation, and the usual [one-outstanding-read limit](model.md#outstanding-operation-limits) applies for the whole duration of the composite operation.

### `as_bytes` / `as_writable_bytes`

`coio::as_bytes(args...)` is `std::as_bytes(std::span(args...))`; `coio::as_writable_bytes` likewise. They accept anything `std::span`'s deduction accepts — arrays, `(pointer, size)`, contiguous ranges:

```cpp
char line[256];
co_await sock.async_read_some(coio::as_writable_bytes(line));
co_await sock.async_write_some(coio::as_bytes(line, n));       // first n chars
std::string_view greeting = "hi";
co_await sock.async_write_some(coio::as_bytes(greeting));
```

## Example

An HTTP-ish request reader using `async_read_until` with `coio::streambuf` (adapted from `examples/http_server/`):

```cpp
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/utils/streambuf.h>

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_socket = coio::tcp::socket<io_context::scheduler>;

auto read_request_head(tcp_socket& socket) -> io_context::task<std::string> {
    coio::streambuf buf;
    auto [ec, head_len] = co_await coio::async_read_until(socket, buf, "\r\n\r\n");
    if (ec) throw std::system_error{ec, "read_request_head"};

    const auto data = buf.data();   // may hold bytes beyond the delimiter
    std::string head{reinterpret_cast<const char*>(data.data()), head_len};
    buf.consume(head_len);          // keep any pipelined bytes in `buf`
    co_return head;
}
```

## See also

- [I/O object model](model.md) — the `*_some` contracts these algorithms build on
- [Files](files.md), [Pipes](pipes.md), [Sockets](../net/sockets.md) — the devices
- [Buffers](../utils/buffers.md) — `flat_buffer`, `streambuf`
- [Error handling](../error-handling.md) — `coio::error::eof` and friends
