# Socket Types

**Header:** `<coio/net/socket.h>`

---

## `coio::basic_socket`

The base class for all socket types.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `Protocol` | Protocol descriptor (e.g., `tcp`, `udp`). |
| `IoScheduler` | Must model `io_scheduler`. |

### Member Types

| Type | Definition |
|------|-----------|
| `protocol_type` | `Protocol` |
| `scheduler_type` | `IoScheduler` |
| `native_handle_type` | `socket_native_handle_type` (platform-specific) |
| `shutdown_type` | Enum: `shutdown_send`, `shutdown_receive`, `shutdown_both` |

### Socket Options

| Option | Description |
|--------|-------------|
| `broadcast` | `SO_BROADCAST` |
| `debug` | `SO_DEBUG` |
| `do_not_route` | `SO_DONTROUTE` |
| `keep_alive` | `SO_KEEPALIVE` |
| `linger` | `SO_LINGER` |
| `out_of_band_inline` | `SO_OOBINLINE` |
| `receive_buffer_size` | `SO_RCVBUF` |
| `receive_low_watermark` | `SO_RCVLOWAT` |
| `reuse_address` | `SO_REUSEADDR` |
| `send_buffer_size` | `SO_SNDBUF` |
| `send_low_watermark` | `SO_SNDLOWAT` |
| `v6_only` | `IPV6_V6ONLY` |

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `basic_socket(scheduler)` | Default. Creates an unopened socket. |
| `basic_socket(scheduler, native_handle)` | Wraps an existing native socket. |
| `basic_socket(scheduler, protocol)` | Opens a socket with the given protocol. |
| *(move-only)* | Move constructible and move assignable. |
| `~basic_socket()` | Destructor. Closes the socket if open. |

#### Core Operations

| Name | Description |
|------|-------------|
| `get_io_scheduler() -> scheduler_type` | Returns the associated scheduler. |
| `native_handle() -> native_handle_type` | Returns the native socket handle. |
| `open(protocol = protocol_type()) -> void` | Opens the socket. |
| `close() -> void` | Closes the socket. |
| `release() -> native_handle_type` | Releases ownership. Does not close. |
| `cancel() -> void` | Cancels all pending async operations. |
| `shutdown(shutdown_type) -> void` | Shuts down send/receive. |

#### Observers

| Name | Description |
|------|-------------|
| `is_open() const -> bool` | Returns `true` if the socket is open. |
| `explicit operator bool() const noexcept` | Same as `is_open()`. |
| `local_endpoint() const -> endpoint` | Returns the local address/port. |
| `remote_endpoint() const -> endpoint` | Returns the remote address/port. |

#### Options

| Name | Description |
|------|-------------|
| `set_option(SocketOption) -> void` | Sets a socket option. |
| `get_option(SocketOption&) -> void` | Gets a socket option. |

#### Operations

| Name | Description |
|------|-------------|
| `bind(endpoint) -> void` | Binds to a local endpoint. |
| `connect(endpoint) -> void` | Synchronous connect. |
| `async_connect(endpoint)` | Returns a sender that completes when connected. |

---

## `coio::basic_socket_acceptor`

**Inherits:** `basic_socket<Protocol, IoScheduler>`

### Member Types

| Type | Description |
|------|-------------|
| `rebind_scheduler<OtherScheduler>` | Rebind to a different scheduler type. |

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `basic_socket_acceptor(scheduler, endpoint, backlog = max_backlog(), reuse_addr = true)` | Opens, binds, and listens in one step. |

#### Operations

| Name | Description |
|------|-------------|
| `static max_backlog() -> std::size_t` | Returns the maximum backlog value. |
| `listen(backlog = max_backlog()) -> void` | Start listening. |

#### Accept (Synchronous)

| Name | Description |
|------|-------------|
| `accept(peer_socket&)` | Accepts into an existing socket. |
| `accept()` | Accepts and returns a default socket type. |
| `accept(other_scheduler)` | Accepts and returns a socket bound to the given scheduler. |

#### Accept (Asynchronous)

| Name | Description |
|------|-------------|
| `async_accept(peer_socket&)` | Returns a sender of `void`. |
| `async_accept()` | Returns a sender of default socket. |
| `async_accept(other_scheduler)` | Returns a sender of socket bound to the given scheduler. |

---

## `coio::basic_stream_socket`

**Inherits:** `basic_socket<Protocol, IoScheduler>`

### Member Types

| Type | Description |
|------|-------------|
| `rebind_scheduler<OtherScheduler>` | Rebind to a different scheduler type. |

### Member Functions

#### Synchronous I/O

| Name | Description |
|------|-------------|
| `read_some(span<byte>) -> size_t` | Reads at least one byte. |
| `write_some(span<const byte>) -> size_t` | Writes at least one byte. |
| `receive(span<byte>) -> size_t` | Alias for `read_some`. |
| `send(span<const byte>) -> size_t` | Alias for `write_some`. |

#### Asynchronous I/O

| Name | Description |
|------|-------------|
| `async_read_some(span<byte>)` | Returns a sender of `size_t`. |
| `async_write_some(span<const byte>)` | Returns a sender of `size_t`. |
| `async_receive(span<byte>)` | Alias for `async_read_some`. |
| `async_send(span<const byte>)` | Alias for `async_write_some`. |

### Out-of-band Data

When using `read_some`/`write_some`:
- A return value of `0` from `read_some` indicates EOF (peer closed connection, reported as `error::misc_errc::eof`).
- Each operation is guaranteed to transfer at least one byte (unless EOF or error).

### Example

```cpp
auto handle_connection(coio::tcp::socket<MyScheduler> sock) -> coio::task<> {
    char buf[1024];
    while (true) {
        auto n = co_await sock.async_read_some(coio::as_writable_bytes(buf));
        co_await coio::async_write(sock, coio::as_bytes(buf, n));
    }
}
```

---

## `coio::basic_datagram_socket`

**Inherits:** `basic_socket<Protocol, IoScheduler>`

### Member Types

| Type | Description |
|------|-------------|
| `rebind_scheduler<OtherScheduler>` | Rebind to a different scheduler type. |

### Member Functions

#### Synchronous I/O

| Name | Description |
|------|-------------|
| `receive(span<byte>) -> size_t` | Receives a datagram (connected socket). |
| `send(span<const byte>) -> size_t` | Sends a datagram (connected socket). |
| `receive_from(span<byte>) -> pair<endpoint, size_t>` | Receives a datagram and the sender endpoint. |
| `send_to(span<const byte>, endpoint) -> size_t` | Sends a datagram to a specific endpoint. |

#### Asynchronous I/O

| Name | Description |
|------|-------------|
| `async_receive(span<byte>)` | Returns a sender of `size_t`. |
| `async_send(span<const byte>)` | Returns a sender of `size_t`. |
| `async_receive_from(span<byte>)` | Returns a sender of `(endpoint, size_t)`. |
| `async_send_to(span<const byte>, endpoint)` | Returns a sender of `size_t`. |

### Example

```cpp
auto udp_echo(MyScheduler sched) -> coio::task<> {
    coio::udp::socket socket{sched, coio::udp::v4()};
    socket.bind(coio::endpoint{coio::ipv4_address::any(), 8080});

    char buf[1500];
    while (true) {
        auto [peer, n] = co_await socket.async_receive_from(
            coio::as_writable_bytes(buf));
        co_await socket.async_send_to(
            coio::as_bytes(buf, n), peer);
    }
}
```
