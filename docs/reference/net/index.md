# Networking

The networking layer provides TCP and UDP socket support on Linux and Windows.

## Headers

| Area | Header |
|------|--------|
| Address types | `<coio/net/basic.h>` |
| TCP protocol | `<coio/net/tcp.h>` |
| UDP protocol | `<coio/net/udp.h>` |
| Socket types | `<coio/net/socket.h>` |
| Resolver | `<coio/net/resolver.h>` |

## Type Hierarchy

```
basic_socket<Protocol, IoScheduler>
├── basic_socket_acceptor<Protocol, IoScheduler>
├── basic_stream_socket<Protocol, IoScheduler>
└── basic_datagram_socket<Protocol, IoScheduler>
```

## Concurrency Rules

coio sockets follow Asio-style concurrency rules:

### Thread Safety

Socket and acceptor objects are **not thread-safe**. Member functions must not be called concurrently on the same object from multiple threads without external synchronization.

If you drive the owning execution context from a single thread, and ensure all socket/acceptor operations are initiated from work running on that thread, that thread acts as an "implicit strand" — operations are serialized by construction.

### Outstanding Operation Limits

Even from a single thread, there are limits on how many async operations can be outstanding simultaneously:

| Socket Type | Max Read | Max Write | Overlap |
|-------------|----------|-----------|---------|
| Stream socket | 1 outstanding read | 1 outstanding write | One read + one write is OK |
| Datagram socket | 1 outstanding receive | 1 outstanding send | One recv + one send is OK |
| Acceptor | 1 outstanding accept | — | — |

**Wrong** — two reads outstanding:

```cpp
char buf1[1024];
char buf2[1024];

co_await coio::when_all(
    sock.async_read_some(coio::as_writable_bytes(buf1)),  // BAD
    sock.async_read_some(coio::as_writable_bytes(buf2))   // BAD
);
```

**Correct** — one read + one write:

```cpp
char read_buf[1024];
std::string_view write_buf = "ping";

co_await coio::when_all(
    sock.async_read_some(coio::as_writable_bytes(read_buf)),  // OK
    sock.async_write_some(coio::as_bytes(write_buf))          // OK
);
```

## Platform Support

| Feature | Linux | Windows |
|---------|-------|---------|
| TCP | epoll / io_uring | IOCP |
| UDP | epoll / io_uring | IOCP |
| Resolver | Yes | Yes |
| IPv4 | Yes | Yes |
| IPv6 | Yes | Yes |
