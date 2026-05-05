# Dynamic Buffers

---

## `coio::basic_flat_buffer`

**Header:** `<coio/utils/flat_buffer.h>`

A dynamic buffer backed by a contiguous block of memory. Models `dynamic_buffer`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `Alloc` | Allocator type. |

### Type Alias

```cpp
using flat_buffer = basic_flat_buffer<std::allocator<std::byte>>;
```

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `basic_flat_buffer()` | Default constructor. |
| `explicit basic_flat_buffer(const Alloc&)` | With allocator. |
| `explicit basic_flat_buffer(size_t max_size, const Alloc&)` | With max size and allocator. |
| Copy/move constructible and assignable. | |

#### Observers

| Name | Description |
|------|-------------|
| `get_allocator() -> allocator_type` | Returns the allocator. |
| `size() const -> size_t` | Current data size. |
| `empty() const -> bool` | |
| `max_size() const -> size_t` | Maximum configurable size. |
| `capacity() const -> size_t` | Current allocated capacity. |
| `data() -> span<byte>` | Mutable view of the data area. |
| `data() const -> span<const byte>` | Const view. |
| `cdata() const -> span<const byte>` | Const view. |

#### Buffer Protocol (dynamic_buffer)

| Name | Description |
|------|-------------|
| `prepare(size_t n) -> span<byte>` | Returns a writable span of at least `n` bytes. |
| `commit(size_t n) -> void` | Marks `n` bytes as written. |
| `consume(size_t n) -> void` | Removes `n` bytes from the front. |

#### Management

| Name | Description |
|------|-------------|
| `clear() -> void` | Resets to empty. |
| `reserve(size_t n) -> void` | Ensures capacity >= `n`. |
| `shrink_to_fit() -> void` | Releases excess capacity. |
| `swap(basic_flat_buffer&) / friend swap` | Swaps two buffers. |

### Example

```cpp
#include <coio/utils/flat_buffer.h>
#include <coio/asyncio/io.h>

auto read_message(auto& sock) -> coio::task<std::string> {
    coio::flat_buffer buf;
    // Read until newline
    auto n = co_await coio::async_read_until(sock, buf, '\n');
    auto data = buf.data();
    std::string msg{reinterpret_cast<const char*>(data.data()), n};
    buf.consume(n);
    co_return msg;
}
```

---

## `coio::basic_streambuf`

**Header:** `<coio/utils/streambuf.h>`

A dynamic buffer that inherits from `std::streambuf`. Models `dynamic_buffer`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `Allocator` | Allocator type. |

### Type Alias

```cpp
using streambuf = basic_streambuf<std::allocator<char>>;
```

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `basic_streambuf(size_t max_size, const Allocator&)` | Constructor with max size and allocator. |

#### Observers

| Name | Description |
|------|-------------|
| `size() const -> size_t` | Current data size. |
| `max_size() const -> size_t` | Maximum size. |
| `capacity() const -> size_t` | Current capacity. |
| `data() const -> span<const byte>` | Const view of the data. |

#### Buffer Protocol (dynamic_buffer)

| Name | Description |
|------|-------------|
| `prepare(size_t n) -> span<byte>` | Returns a writable span. |
| `commit(size_t n) -> void` | Marks bytes as written. |
| `consume(size_t n) -> void` | Removes bytes from the front. |

### When to use which

| Use Case | Recommended Buffer |
|----------|-------------------|
| General async I/O | `flat_buffer` |
| Need `std::streambuf` integration (ostream) | `streambuf` |
| Custom allocator required | `basic_flat_buffer<Alloc>` |
