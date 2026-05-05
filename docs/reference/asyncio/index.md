# Async I/O Concepts

**Header:** `<coio/asyncio/io.h>`

This section documents the I/O device concepts and the free I/O algorithm functions.

---

## Device Concepts

### Synchronous Input

```cpp
template<typename T>
concept input_stream_device =
    requires(T t, std::span<std::byte> buffer) {
        { t.read_some(buffer) } -> std::integral;
    };

template<typename T>
concept input_random_access_device =
    requires(T t, std::size_t offset, std::span<std::byte> buffer) {
        { t.read_some_at(offset, buffer) } -> std::integral;
    };
```

### Synchronous Output

```cpp
template<typename T>
concept output_stream_device =
    requires(T t, std::span<const std::byte> buffer) {
        { t.write_some(buffer) } -> std::integral;
    };

template<typename T>
concept output_random_access_device =
    requires(T t, std::size_t offset, std::span<const std::byte> buffer) {
        { t.write_some_at(offset, buffer) } -> std::integral;
    };
```

### Composite Concepts

```cpp
template<typename T>
concept stream_device = input_stream_device<T> && output_stream_device<T>;

template<typename T>
concept random_access_device =
    input_random_access_device<T> && output_random_access_device<T>;
```

### Async Input

```cpp
template<typename T>
concept async_input_stream_device =
    requires(T t, std::span<std::byte> buffer) {
        { t.async_read_some(buffer) } -> execution::sender;
    };

template<typename T>
concept async_input_random_access_device =
    requires(T t, std::size_t offset, std::span<std::byte> buffer) {
        { t.async_read_some_at(offset, buffer) } -> execution::sender;
    };
```

### Async Output

```cpp
template<typename T>
concept async_output_stream_device =
    requires(T t, std::span<const std::byte> buffer) {
        { t.async_write_some(buffer) } -> execution::sender;
    };

template<typename T>
concept async_output_random_access_device =
    requires(T t, std::size_t offset, std::span<const std::byte> buffer) {
        { t.async_write_some_at(offset, buffer) } -> execution::sender;
    };
```

### Composite Async Concepts

```cpp
template<typename T>
concept async_stream_device =
    async_input_stream_device<T> && async_output_stream_device<T>;

template<typename T>
concept async_random_access_device =
    async_input_random_access_device<T> && async_output_random_access_device<T>;
```

## Dynamic Buffer Concept

```cpp
template<typename T>
concept dynamic_buffer = requires(T t, const T& ct, std::size_t n) {
    { ct.size() } -> std::integral;
    { ct.capacity() } -> std::integral;
    { ct.max_size() } -> std::integral;
    { ct.data() } -> std::convertible_to<std::span<const std::byte>>;
    { t.prepare(n) } -> std::convertible_to<std::span<std::byte>>;
    t.commit(n);
    t.consume(n);
};
```

Both [flat_buffer](../utils/buffers.md) and [streambuf](../utils/buffers.md) satisfy `dynamic_buffer`.

## Which Types Satisfy These Concepts?

| Type | Concepts Satisfied |
|------|-------------------|
| `basic_stream_socket` | `async_stream_device`, `stream_device` |
| `basic_datagram_socket` | `async_stream_device`, `stream_device` |
| `stream_file` | `async_stream_device`, `stream_device` |
| `random_access_file` | `async_random_access_device`, `random_access_device` |
| `pipe_reader` | `async_input_stream_device` |
| `pipe_writer` | `async_output_stream_device` |
