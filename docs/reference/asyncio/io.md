# I/O Algorithms

**Header:** `<coio/asyncio/io.h>`

Free functions for synchronous and asynchronous I/O operations. All are `inline constexpr` function objects.

---

## Synchronous I/O

### `coio::read`

Reads exactly `buffer.size()` bytes from a stream device.

```cpp
auto read(input_stream_device auto& device, std::span<std::byte> buffer) -> std::size_t;
```
- Returns: total bytes read (equals `buffer.size()`).
- Loops internally calling `device.read_some()` until the full buffer is filled or an error occurs.

```cpp
auto read(input_stream_device auto& device,
          dynamic_buffer auto& dyn_buffer,
          std::size_t total) -> std::size_t;
```
- Prepares `total` bytes, reads into the buffer, commits the data.

### `coio::write`

Writes exactly `buffer.size()` bytes to a stream device.

```cpp
auto write(output_stream_device auto& device,
           std::span<const std::byte> buffer) -> std::size_t;

auto write(output_stream_device auto& device,
           dynamic_buffer auto& dyn_buffer) -> std::size_t;
```

### `coio::read_at`

Reads from a specific offset on a random-access device.

```cpp
auto read_at(input_random_access_device auto& device,
             std::size_t offset,
             std::span<std::byte> buffer) -> std::size_t;

auto read_at(input_random_access_device auto& device,
             std::size_t offset,
             dynamic_buffer auto& dyn_buffer,
             std::size_t total) -> std::size_t;
```

### `coio::write_at`

Writes at a specific offset to a random-access device.

```cpp
auto write_at(output_random_access_device auto& device,
              std::size_t offset,
              std::span<const std::byte> buffer) -> std::size_t;

auto write_at(output_random_access_device auto& device,
              std::size_t offset,
              dynamic_buffer auto& dyn_buffer) -> std::size_t;
```

### `coio::read_until`

Reads from a stream device until a delimiter is found.

```cpp
// Read until char delimiter
auto read_until(input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                char delim) -> std::size_t;

// Read until string delimiter
auto read_until(input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                std::string_view delim) -> std::size_t;
```

- Returns: total bytes in the buffer up to and including the delimiter, or `0` if EOF is reached.

---

## Asynchronous I/O

All async functions return **senders** that complete with `(std::error_code, std::size_t)`.

### `coio::async_read`

```cpp
auto async_read(async_input_stream_device auto& device,
                std::span<std::byte> buffer);

auto async_read(async_input_stream_device auto& device,
                dynamic_buffer auto& dyn_buffer,
                std::size_t total);
```

The sender completes with `(error_code, bytes_transferred)`.

### `coio::async_write`

```cpp
auto async_write(async_output_stream_device auto& device,
                 std::span<const std::byte> buffer);

auto async_write(async_output_stream_device auto& device,
                 dynamic_buffer auto& dyn_buffer);
```

### `coio::async_read_at`

```cpp
auto async_read_at(async_input_random_access_device auto& device,
                   std::size_t offset,
                   std::span<std::byte> buffer);

auto async_read_at(async_input_random_access_device auto& device,
                   std::size_t offset,
                   dynamic_buffer auto& dyn_buffer,
                   std::size_t total);
```

### `coio::async_write_at`

```cpp
auto async_write_at(async_output_random_access_device auto& device,
                    std::size_t offset,
                    std::span<const std::byte> buffer);

auto async_write_at(async_output_random_access_device auto& device,
                    std::size_t offset,
                    dynamic_buffer auto& dyn_buffer);
```

### `coio::async_read_until`

```cpp
// Read until char delimiter
auto async_read_until(async_input_stream_device auto& device,
                      dynamic_buffer auto& buffer,
                      char delim);

// Read until string delimiter
auto async_read_until(async_input_stream_device auto& device,
                      dynamic_buffer auto& buffer,
                      std::string_view delim);
```

- The sender completes with `(error_code, bytes_transferred)`.
- `bytes_transferred` is the number of bytes up to and including the delimiter.

---

## Completion and Error Handling

When awaiting these senders inside a `coio::task`, the library automatically adapts the `(error_code, bytes_transferred)` result:

- **Success** (`error_code == 0`): the sender completes with `set_value(bytes_transferred)`.
- **Cancellation**: the sender completes with `set_stopped()`.
- **Error**: the sender completes with `set_error(error_code)`.

This means you can write:

```cpp
std::size_t n = co_await coio::async_read(socket, coio::as_writable_bytes(buf));
// n contains bytes_transferred; errors are thrown as std::system_error
```

---

## Span Helpers

### `coio::as_bytes`

```cpp
auto as_bytes(Args&&... args) -> std::span<const std::byte>;
```

Converts any span-constructible arguments to a byte span.

### `coio::as_writable_bytes`

```cpp
auto as_writable_bytes(Args&&... args) -> std::span<std::byte>;
```

Converts any span-constructible arguments to a mutable byte span.

---

## Example

```cpp
#include <coio/asyncio/io.h>
#include <coio/utils/flat_buffer.h>

auto read_headers(auto& sock) -> coio::task<> {
    coio::flat_buffer buf;
    // Read until "\r\n\r\n" (end of HTTP headers)
    std::size_t n = co_await coio::async_read_until(sock, buf, "\r\n\r\n");
    std::println("Read {} bytes", n);

    auto data = buf.data();
    std::string_view headers{reinterpret_cast<const char*>(data.data()), n};
    std::println("Headers:\n{}", headers);
}
```
