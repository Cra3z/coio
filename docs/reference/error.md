# Error Handling

**Header:** `<coio/detail/error.h>` (included transitively via most public headers)

---

## Error Codes

### `coio::error::misc_errc`

```cpp
namespace coio::error {
    enum misc_errc : int {
        eof           = 1,
        already_open  = 2,
        not_found     = 3,
        overflow      = 4
    };
}
```

| Enumerator | Value | Description |
|------------|-------|-------------|
| `eof` | 1 | End of file or stream. Connection closed by peer. |
| `already_open` | 2 | Attempted to open an already-open resource. |
| `not_found` | 3 | Resource not found. |
| `overflow` | 4 | Buffer overflow or capacity exceeded. |

### Error Category

```cpp
auto coio::error::misc_category() noexcept -> const std::error_category&;
auto coio::error::make_error_code(misc_errc) noexcept -> std::error_code;
```

The standard `std::is_error_code_enum` is specialized for `misc_errc`, enabling implicit conversion to `std::error_code`.

### getaddrinfo Error Category

```cpp
auto coio::error::gai_category() noexcept -> const std::error_category&;
```

Returns the error category for `getaddrinfo` errors (used by the resolver).

---

## Usage

```cpp
#include <coio/net/socket.h>
#include <coio/asyncio/io.h>

auto echo_client(auto& sock) -> coio::task<> {
    try {
        char buf[1024];
        while (true) {
            auto n = co_await sock.async_read_some(
                coio::as_writable_bytes(buf));
            co_await coio::async_write(sock, coio::as_bytes(buf, n));
        }
    } catch (const std::system_error& e) {
        if (e.code() == coio::error::misc_errc::eof) {
            std::println("Connection closed by peer");
            co_return;
        }
        std::println("Error: {}", e.what());
        throw;
    }
}
```

When `eof` is signaled inside `read_some`/`async_read_some` (stream sockets or files), the library wraps it in a `std::system_error` with code `misc_errc::eof`.

## Sender Error Channel

When using the raw sender API (not `co_await`), errors are delivered via the sender's error completion channel:

```
set_value_t(std::error_code, std::size_t)  // I/O senders
set_error_t(std::exception_ptr)            // task errors
set_stopped_t()                            // cancellation
```

The `co_await` integration in `coio::task` converts these channels into exceptions automatically.
