# Sockets

The socket class templates: `basic_socket` (open/bind/connect/options — the common base), `basic_stream_socket` (connected byte streams, e.g. TCP), `basic_datagram_socket` (datagrams, e.g. UDP) and `basic_socket_acceptor` (listening/accepting). In practice you name them through the [protocol descriptors](protocols.md): `coio::tcp::socket<S>`, `coio::tcp::acceptor<S>`, `coio::udp::socket<S>`. This page also states the socket **concurrency rules** — thread-safety, outstanding-operation limits and lifetime — which every user of the async API must follow.

Header: `#include <coio/net/socket.h>`

## Overview

```text
basic_socket<Protocol, IoScheduler>          open/close/cancel/bind/connect/shutdown/options
├── basic_stream_socket<Protocol, S>         read_some/write_some (+ receive/send aliases), async_*
├── basic_datagram_socket<Protocol, S>       receive/send, receive_from/send_to, async_*
└── basic_socket_acceptor<Protocol, S>       listen, accept, async_accept
```

All socket types are **move-only**, are parameterized on an [`io_scheduler`](../io/model.md), own a backend io object created via `scheduler.make_io_object(...)`, and follow the [I/O object model](../io/model.md) for cancellation, `close()` and lifetime. `native_handle_type` is `int` on Linux and `UINT_PTR` (`SOCKET`) on Windows.

## Synopsis

```cpp
namespace coio {
    template<typename Protocol, io_scheduler IoScheduler>
    class basic_socket {
    public:
        using protocol_type = Protocol;
        using scheduler_type = IoScheduler;
        using native_handle_type = /* int on Linux, UINT_PTR on Windows */;
        using shutdown_type = /* enum: shutdown_send, shutdown_receive, shutdown_both */;
        using enum shutdown_type;

        // socket option types (see table below)
        using broadcast = ...;             using debug = ...;
        using do_not_route = ...;          using keep_alive = ...;
        using linger = ...;                using out_of_band_inline = ...;
        using receive_buffer_size = ...;   using receive_low_watermark = ...;
        using reuse_address = ...;         using send_buffer_size = ...;
        using send_low_watermark = ...;    using v6_only = ...;

        explicit basic_socket(scheduler_type scheduler) noexcept;       // closed socket
        basic_socket(scheduler_type scheduler, native_handle_type handle);  // adopt handle
        basic_socket(scheduler_type scheduler, const protocol_type& protocol); // open
        basic_socket(basic_socket&&) = default;                          // move-only

        auto get_io_scheduler() const noexcept -> scheduler_type;
        auto native_handle() const noexcept -> native_handle_type;
        auto open(const protocol_type& protocol = protocol_type()) -> void;
        auto close() -> void;
        auto cancel() -> void;
        auto shutdown(shutdown_type how) -> void;
        auto is_open() const noexcept -> bool;
        explicit operator bool() const noexcept;
        auto local_endpoint() const -> endpoint;
        auto remote_endpoint() const -> endpoint;
        template<typename SocketOption> auto set_option(const SocketOption&) -> void;
        template<typename SocketOption> auto get_option(SocketOption&) const -> void;
        auto bind(const endpoint& local_endpoint) -> void;
        auto connect(const endpoint& peer) -> void;
        auto async_connect(const endpoint& peer);          // sender of void
    };

    template<typename Protocol, io_scheduler IoScheduler>
    class basic_socket_acceptor : public basic_socket<Protocol, IoScheduler> {
    public:
        template<io_scheduler S> using rebind_scheduler = basic_socket_acceptor<Protocol, S>;

        using basic_socket<Protocol, IoScheduler>::basic_socket;
        basic_socket_acceptor(scheduler_type scheduler, const endpoint& local_endpoint,
                              std::size_t backlog = max_backlog(), bool reuse_addr = true);

        static auto max_backlog() noexcept -> std::size_t;
        auto listen(std::size_t backlog = max_backlog()) -> void;

        template<io_scheduler S> auto accept(Protocol::socket<S>& peer) -> void;
        template<io_scheduler S> auto accept(S peer_scheduler) -> Protocol::socket<S>;
        auto accept() -> Protocol::socket<scheduler_type>;

        template<io_scheduler S> auto async_accept(Protocol::socket<S>& peer);  // sender of void
        template<io_scheduler S> auto async_accept(S other_scheduler);          // sender of Protocol::socket<S>
        auto async_accept();                             // sender of Protocol::socket<scheduler_type>
    };

    template<typename Protocol, io_scheduler IoScheduler>
    class basic_stream_socket : public basic_socket<Protocol, IoScheduler> {
    public:
        template<io_scheduler S> using rebind_scheduler = basic_stream_socket<Protocol, S>;

        auto read_some(std::span<std::byte> buffer) -> std::size_t;
        auto write_some(std::span<const std::byte> buffer) -> std::size_t;
        auto receive(std::span<std::byte> buffer) -> std::size_t;        // = read_some
        auto send(std::span<const std::byte> buffer) -> std::size_t;     // = write_some
        auto async_read_some(std::span<std::byte> buffer);               // sender of std::size_t
        auto async_write_some(std::span<const std::byte> buffer);        // sender of std::size_t
        auto async_receive(std::span<std::byte> buffer);                 // = async_read_some
        auto async_send(std::span<const std::byte> buffer);              // = async_write_some
    };

    template<typename Protocol, io_scheduler IoScheduler>
    class basic_datagram_socket : public basic_socket<Protocol, IoScheduler> {
    public:
        template<io_scheduler S> using rebind_scheduler = basic_datagram_socket<Protocol, S>;

        auto receive(std::span<std::byte> buffer) -> std::size_t;
        auto send(std::span<const std::byte> buffer) -> std::size_t;
        auto receive_from(std::span<std::byte> buffer) -> std::pair<endpoint, std::size_t>;
        auto send_to(std::span<const std::byte> buffer, const endpoint& peer) -> std::size_t;
        auto async_receive(std::span<std::byte> buffer);                 // sender of std::size_t
        auto async_send(std::span<const std::byte> buffer);              // sender of std::size_t
        auto async_receive_from(std::span<std::byte> buffer);            // sender of (endpoint, std::size_t)
        auto async_send_to(std::span<const std::byte> buffer, const endpoint& peer); // sender of std::size_t
    };
}
```

Synchronous members throw `std::system_error` on failure. Asynchronous members return lazy senders completing with `set_value(...)`, `set_error(std::error_code)` or `set_stopped()` (on cancellation), delivered on the owning context's consumer thread.

## API Reference

### `basic_socket`

#### Constructors
- `basic_socket(scheduler)` — a closed socket bound to a scheduler.
- `basic_socket(scheduler, native_handle)` — adopts an existing native socket (ownership transfers; on `iocp_context` it is associated with the completion port and must have been created for overlapped I/O; on `epoll_context` it is switched to `O_NONBLOCK`).
- `basic_socket(scheduler, protocol)` — creates and opens (`open(protocol)`).

#### `open(const protocol_type& protocol = protocol_type()) -> void`
Creates the OS socket with the protocol's `family()`/`type()`/`protocol_id()`. Throws `std::system_error` with `coio::error::already_open` if already open, or an OS error.

#### `close() -> void`
Closes the OS socket and resets the object to the not-open state. **Precondition: no outstanding asynchronous operations** — every operation must have completed before the call (`cancel()` first and await the completions if needed). Throws `std::system_error` on failure. The destructor closes implicitly, under the same precondition. See [`close()` per backend](../io/model.md#close) and the lifetime rules below.

#### `cancel() -> void`
Requests cancellation of the outstanding asynchronous operations on this socket; cancelled operations complete with `set_stopped()` (an operation that has already produced its result still delivers `set_value`/`set_error`). The socket stays open.

#### `shutdown(shutdown_type how) -> void`
Disables sends (`shutdown_send`), receives (`shutdown_receive`), or both (`shutdown_both`) at the protocol level. Unlike `close()`, the descriptor stays valid; a TCP peer sees FIN. Throws `std::system_error` on failure.

#### `is_open()` / `explicit operator bool()`
Whether the socket holds a valid native handle.

#### `local_endpoint()` / `remote_endpoint() -> endpoint`
The locally bound / connected peer address. Throw `std::system_error` on failure (e.g. unbound or unconnected socket).

#### `bind(const endpoint& local_endpoint) -> void`
Binds to a local address/port. Throws `std::system_error` on failure.

#### `connect(const endpoint& peer) -> void`
Blocking connect. If the socket is not open, it is first opened with the protocol matching `peer`'s address family (`protocol_type::v4()` or `protocol_type::v6()`). Throws `std::system_error` on failure.

#### `async_connect(const endpoint& peer)`
Returns a sender of `void`. When started, opens the socket first if necessary (protocol derived from `peer`'s family, as above), then connects. Completes with `set_value()`, `set_error(std::error_code)`, or `set_stopped()`.

#### `set_option(const SocketOption&)` / `get_option(SocketOption&) const`
Set/query a socket option; any type with `level()`, `name()` and `data()` members works, normally one of the types below. Throw `std::system_error` on failure.

#### Socket options

Each option type is constructible from its value type and has `get() -> value_type` and `set(value)`. Aliases for all of these are nested in `basic_socket` (e.g. `tcp_socket::reuse_address`); `no_delay` is nested in `coio::tcp`.

| Option type | Value type | Underlying option |
|-------------|-----------|-------------------|
| `broadcast` | `bool` | `SO_BROADCAST` |
| `debug` | `bool` | `SO_DEBUG` |
| `do_not_route` | `bool` | `SO_DONTROUTE` |
| `keep_alive` | `bool` | `SO_KEEPALIVE` |
| `linger` | `::linger` | `SO_LINGER` |
| `out_of_band_inline` | `bool` | `SO_OOBINLINE` |
| `receive_buffer_size` | `int` | `SO_RCVBUF` |
| `receive_low_watermark` | `int` | `SO_RCVLOWAT` |
| `reuse_address` | `bool` | `SO_REUSEADDR` |
| `send_buffer_size` | `int` | `SO_SNDBUF` |
| `send_low_watermark` | `int` | `SO_SNDLOWAT` |
| `v6_only` | `bool` | `IPV6_V6ONLY` (IPv6 level) |
| `tcp::no_delay` | `bool` | `TCP_NODELAY` (TCP level) |

```cpp
socket.set_option(tcp_socket::keep_alive{true});
tcp_socket::send_buffer_size size;
socket.get_option(size);
std::size_t n = size.get();
```

### `basic_socket_acceptor`

#### `basic_socket_acceptor(scheduler, const endpoint& local_endpoint, std::size_t backlog = max_backlog(), bool reuse_addr = true)`
Convenience constructor: opens with the protocol matching the endpoint's IP version, sets `reuse_address{reuse_addr}`, binds to `local_endpoint`, and listens with `backlog`. Throws `std::system_error` on any failure.

#### `max_backlog() noexcept -> std::size_t` *(static)*
The OS maximum listen-queue length (`SOMAXCONN`).

#### `listen(std::size_t backlog = max_backlog()) -> void`
Starts listening for connections. Throws `std::system_error` on failure.

#### `accept` (blocking)
- `accept() -> Protocol::socket<scheduler_type>` — accepts one connection; the new socket uses the acceptor's scheduler.
- `accept(OtherScheduler peer_scheduler) -> Protocol::socket<OtherScheduler>` — the new socket completes its I/O on a *different* scheduler (e.g. a per-worker context).
- `accept(Protocol::socket<OtherScheduler>& peer) -> void` — accepts into an existing socket object (replaces it), keeping `peer`'s scheduler.

All throw `std::system_error` on failure.

#### `async_accept`
- `async_accept()` — sender of `Protocol::socket<scheduler_type>`.
- `async_accept(OtherScheduler other_scheduler)` — sender of `Protocol::socket<OtherScheduler>`; the accepted socket is bound to `other_scheduler` (cross-scheduler accept, used by context-pool servers).
- `async_accept(Protocol::socket<OtherScheduler>& peer)` — sender of `void`; on completion, `peer` is replaced by a socket wrapping the accepted connection on `peer`'s scheduler. `peer` must outlive the operation.

All complete with `set_error(std::error_code)` on failure and `set_stopped()` on cancellation. The program must ensure that no other call to `accept`/`async_accept` is made until an outstanding accept completes, and initiating functions must not run concurrently from different threads on the same acceptor (see the concurrency rules below).

### `basic_stream_socket`

#### `read_some(std::span<std::byte> buffer) -> std::size_t` / `receive(...)`
Blocking read of up to `buffer.size()` bytes; returns the count (≥ 1 for a non-empty buffer). A graceful connection close is reported by **throwing `std::system_error` with `coio::error::eof`**. Prefer [`coio::read`](../io/algorithms.md) when you need the full requested amount.

#### `write_some(std::span<const std::byte> buffer) -> std::size_t` / `send(...)`
Blocking write of up to `buffer.size()` bytes; returns the count written. Prefer [`coio::write`](../io/algorithms.md) for complete transfers.

#### `async_read_some(std::span<std::byte> buffer)` / `async_receive(...)`
Sender of `std::size_t`, completing once at least one byte was read. A 0-byte completion for a non-empty buffer (peer closed) is mapped to `set_error(coio::error::eof)`. Prefer [`coio::async_read`](../io/algorithms.md) for complete transfers.

#### `async_write_some(std::span<const std::byte> buffer)` / `async_send(...)`
Sender of `std::size_t` (bytes written, possibly fewer than requested). Prefer [`coio::async_write`](../io/algorithms.md) for complete transfers.

### `basic_datagram_socket`

Datagram operations transfer whole datagrams; a datagram larger than the buffer is truncated. There is no EOF concept — a 0-byte receive is a valid empty datagram. Zero-length operations are **real**, matching asio: an empty `send`/`send_to` transmits an empty datagram, and a zero-length receive waits for and consumes a datagram (the empty-buffer no-op applies to stream sockets only).

#### `receive(buffer)` / `send(buffer)`
Blocking receive/send on a *connected* datagram socket. Return the byte count; throw `std::system_error` on failure.

#### `receive_from(buffer) -> std::pair<endpoint, std::size_t>`
Blocking receive; returns the sender's endpoint and the byte count.

#### `send_to(buffer, const endpoint& peer) -> std::size_t`
Blocking send to an explicit destination.

#### `async_receive(buffer)` / `async_send(buffer)`
Senders of `std::size_t` for connected-mode datagram I/O.

#### `async_receive_from(buffer)`
Sender of `(endpoint, std::size_t)` — the datagram source and size.

#### `async_send_to(buffer, const endpoint& peer)`
Sender of `std::size_t`.

## Concurrency rules

Be careful with the term "concurrency":

- **Concurrent calls** refers to *thread-safety*: two threads calling member functions on the same object at the same time.
- **Outstanding (pending) operations** refers to async operations that have been initiated and have not completed yet.

### Thread safety

Like Asio sockets/streams, coio socket/acceptor objects are **not thread-safe**. In other words, **member functions must not be called concurrently** on the same socket/acceptor from multiple threads unless you provide external synchronization. In particular, the behavior is undefined if two initiating functions (names that start with `async_`) are called on the same socket object from different threads simultaneously.

If all socket/acceptor operations are initiated from work running on the owning execution context's consumer thread, that thread acts as an "implicit strand" (operations are serialized by construction).

Operations may be initiated from threads other than the context consumer, but **all initiating calls for a given socket/acceptor must still be serialized** (e.g. a mutex, or funneling initiation through a single owning thread/task).

### Outstanding-operation limits

This thread-safety rule is independent of how many operations may be outstanding. The async interface supports the following **outstanding-operation** limits (Asio-style):

- **Stream sockets**: at most one outstanding read, and at most one outstanding write.
- **Allowed overlap**: you may have one read and one write outstanding at the same time.
- **Not allowed**: two reads outstanding simultaneously; likewise for writes.
- **Acceptors**: at most one outstanding `accept` / `async_accept` per acceptor.
- **Datagram sockets**: the same one-per-direction rule — `receive`/`receive_from` and their async forms share the read direction; `send`/`send_to` and their async forms share the write direction.

Example: the following is **malformed** because it starts two reads without waiting for the first to complete:

```cpp
// Wrong: two reads outstanding at the same time
co_await when_all(
    sock.async_read_some(buf1),
    sock.async_read_some(buf2)
);
```

But having one read and one write outstanding is **well-formed**:

```cpp
// OK: one read + one write outstanding
co_await when_all(
    sock.async_read_some(read_buf),
    sock.async_write_some(write_buf)
);
```

### Lifetime

An I/O object must outlive all of its operations. A sender obtained from an I/O object (`async_read_some`, `async_receive`, ...) must be connected and started **before** the object is closed or destroyed; starting it afterwards is undefined behavior. On `epoll_context` in particular, `close()` returns the object's per-descriptor bookkeeping entry to an internal pool, so a stale start may silently corrupt the state of an unrelated I/O object that has since reused the entry, rather than failing cleanly with `EBADF`.

Buffers passed to `async_*` operations, and the `peer` argument of `async_accept(peer&)`, are captured by reference/span and must also remain valid until the operation completes.

## EOF behavior

`basic_stream_socket::read_some`/`async_read_some` (and their `receive` aliases) report connection close as `coio::error::misc_errc::eof`: the synchronous form throws `std::system_error{coio::error::eof}`, the asynchronous form completes with `set_error(coio::error::eof)`. Datagram sockets never report EOF.

## Example

A single-threaded TCP echo server (adapted from `examples/tcp_echo_server-single_thread.cpp`):

```cpp
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_socket   = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

auto handle_connection(tcp_socket socket) -> io_context::task<> {
    try {
        char buffer[1024];
        while (true) {
            // one read at a time; the echoing write only starts after the read completed
            const auto n = co_await socket.async_read_some(coio::as_writable_bytes(buffer));
            auto [ec, written] = co_await coio::async_write(socket, coio::as_bytes(buffer, n));
            if (ec) throw std::system_error{ec};
        }
    }
    catch (const std::system_error&) {
        // coio::error::eof: peer closed the connection
    }
}   // socket destroyed only after all its operations completed

auto start_server(coio::async_scope& scope) -> io_context::task<> {
    io_context::scheduler sched = co_await coio::read_scheduler();
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    while (true) {
        // at most one outstanding accept per acceptor
        scope.spawn_on(sched, handle_connection(co_await acceptor.async_accept()));
    }
}

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), start_server(scope));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## See also

- [I/O object model](../io/model.md) — cancellation, `close()` semantics, lifetime, platform notes
- [Protocols](protocols.md) — `tcp` / `udp` descriptors and type aliases
- [Addresses & Endpoints](addresses.md) — `endpoint`, `ip_address`
- [Resolver](resolver.md) — name resolution
- [I/O algorithms](../io/algorithms.md) — `async_read` / `async_write` / `async_read_until`
- [Thread safety](../thread-safety.md), [Error handling](../error-handling.md)
