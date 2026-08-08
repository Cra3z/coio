# Protocols

`coio::tcp` and `coio::udp` are lightweight **protocol descriptors**: small value types that carry the address family (IPv4/IPv6), socket type and protocol number handed to the OS when a socket is opened, plus nested aliases that name the matching socket, acceptor and resolver class templates. You rarely construct sockets from `basic_*_socket` directly — you write `coio::tcp::socket<Scheduler>` instead.

Headers: `#include <coio/net/tcp.h>`, `#include <coio/net/udp.h>`

## Overview

| Descriptor | Kind | Nested aliases | Protocol-specific options |
|------------|------|----------------|---------------------------|
| `coio::tcp` | Stream, connection-oriented | `socket<S>`, `acceptor<S>`, `resolver<S>` | `tcp::no_delay` |
| `coio::udp` | Datagram, connectionless | `socket<S>`, `resolver<S>` | — |

A descriptor instance selects the address family: `tcp::v4()`, `tcp::v6()`, `udp::v4()`, `udp::v6()`. Default construction yields the IPv4 descriptor. Pass the descriptor to `basic_socket::open` (or a socket constructor) to create the OS socket.

## Synopsis

```cpp
namespace coio {
    class tcp {
    public:
        template<io_scheduler IoScheduler>
        using acceptor = basic_socket_acceptor<tcp, IoScheduler>;

        template<io_scheduler IoScheduler>
        using socket = basic_stream_socket<tcp, IoScheduler>;

        template<scheduler Scheduler>
        using resolver = basic_resolver<tcp, Scheduler>;

        using no_delay = /* boolean socket option: TCP_NODELAY */;

        tcp() noexcept;                                  // IPv4
        static auto v4() noexcept -> tcp;
        static auto v6() noexcept -> tcp;

        auto family() const noexcept -> int;             // AF_INET / AF_INET6
        static auto type() noexcept -> int;              // SOCK_STREAM
        static auto protocol_id() noexcept -> int;       // IPPROTO_TCP

        friend auto operator== (const tcp&, const tcp&) noexcept -> bool = default;
    };

    class udp {
    public:
        template<io_scheduler IoScheduler>
        using socket = basic_datagram_socket<udp, IoScheduler>;

        template<scheduler Scheduler>
        using resolver = basic_resolver<udp, Scheduler>;

        udp() noexcept;                                  // IPv4
        static auto v4() noexcept -> udp;
        static auto v6() noexcept -> udp;

        auto family() const noexcept -> int;             // AF_INET / AF_INET6
        static auto type() noexcept -> int;              // SOCK_DGRAM
        static auto protocol_id() noexcept -> int;       // IPPROTO_UDP

        friend auto operator== (const udp&, const udp&) noexcept -> bool = default;
    };
}
```

## API Reference

### Construction and family selection

- **`tcp()` / `udp()`** — default-construct the **IPv4** descriptor (same as `v4()`).
- **`v4()` / `v6()`** — descriptors for the IPv4 / IPv6 variant of the protocol. There are no other constructors; the family is the only per-instance state.
- **`operator==`** — two descriptors are equal iff they have the same family.

### Introspection

- **`family() const noexcept -> int`** — the OS address-family constant (`AF_INET` or `AF_INET6`) this descriptor was created with.
- **`type() noexcept -> int`** *(static)* — the OS socket type: `SOCK_STREAM` for `tcp`, `SOCK_DGRAM` for `udp`.
- **`protocol_id() noexcept -> int`** *(static)* — the OS protocol number: `IPPROTO_TCP` / `IPPROTO_UDP`.

These three values are exactly what `basic_socket::open` passes to `socket(2)`/`WSASocket`.

### Nested aliases

- **`tcp::socket<IoScheduler>`** = `basic_stream_socket<tcp, IoScheduler>` — a connected byte-stream socket.
- **`tcp::acceptor<IoScheduler>`** = `basic_socket_acceptor<tcp, IoScheduler>` — listens and accepts `tcp::socket`s.
- **`udp::socket<IoScheduler>`** = `basic_datagram_socket<udp, IoScheduler>` — a datagram socket. UDP has no acceptor.
- **`tcp::resolver<Scheduler>` / `udp::resolver<Scheduler>`** = `basic_resolver<Protocol, Scheduler>` — name resolution constrained to this protocol's socket type. Note the resolver is parameterized on any `scheduler` (it needs no I/O backend), while sockets require an `io_scheduler`.

### Options

- **`tcp::no_delay`** — boolean option mapping to `TCP_NODELAY` (disable Nagle's algorithm). Use with `set_option`/`get_option` on a `tcp::socket`; see the [socket options table](sockets.md#socket-options).

## Example

```cpp
#include <coio/net/tcp.h>
#include <coio/net/udp.h>
#include <coio/asyncio/epoll_context.h>   // or uring/iocp

using scheduler = coio::epoll_context::scheduler;

using tcp_socket   = coio::tcp::socket<scheduler>;
using tcp_acceptor = coio::tcp::acceptor<scheduler>;
using udp_socket   = coio::udp::socket<scheduler>;

auto make_sockets(scheduler sched) -> void {
    // open an IPv6 TCP socket explicitly
    tcp_socket s6{sched};
    s6.open(coio::tcp::v6());

    // open an IPv4 TCP socket and disable Nagle
    tcp_socket s4{sched, coio::tcp::v4()};
    s4.set_option(coio::tcp::no_delay{true});

    // a v4 UDP socket (udp{} == udp::v4())
    udp_socket u{sched, coio::udp::v4()};
}
```

## See also

- [Sockets](sockets.md) — `basic_socket`, `basic_stream_socket`, `basic_datagram_socket`, `basic_socket_acceptor`
- [Resolver](resolver.md) — `basic_resolver`
- [Addresses & Endpoints](addresses.md) — choosing v4/v6 endpoints
