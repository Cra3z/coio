# coio::udp

**Header:** `<coio/net/udp.h>`

The UDP protocol type. Used to create type-safe UDP socket and resolver instances.

## Member Functions

| Name | Description |
|------|-------------|
| `static v4() -> udp` | Returns a UDP/IPv4 protocol object. |
| `static v6() -> udp` | Returns a UDP/IPv6 protocol object. |
| `family() const -> int` | Returns the address family (`AF_INET` or `AF_INET6`). |
| `static type() -> int` | Returns `SOCK_DGRAM`. |
| `static protocol_id() -> int` | Returns `IPPROTO_UDP`. |
| `operator==(udp, udp) = default` | Comparison. |

Note: constructing a `udp` object directly is not possible; use `udp::v4()` or `udp::v6()`.

## Type Aliases

```cpp
template<io_scheduler IoScheduler>
using socket = basic_datagram_socket<udp, IoScheduler>;

template<scheduler Scheduler>
using resolver = basic_resolver<udp, Scheduler>;
```

Usage:

```cpp
using io_context = coio::iocp_context;
using udp_socket   = coio::udp::socket<io_context::scheduler>;
using udp_resolver = coio::udp::resolver<io_context::scheduler>;
```

## Example

```cpp
#include <coio/net/udp.h>
#include <coio/net/socket.h>
#include <coio/asyncio/io.h>

auto echo_server(io_context& ctx) -> coio::task<> {
    auto sched = ctx.get_scheduler();
    coio::udp::socket socket{sched, coio::udp::v4()};
    socket.bind(coio::endpoint{coio::ipv4_address::any(), 8080});

    char buf[1024];
    while (true) {
        auto [peer, n] = co_await socket.async_receive_from(
            coio::as_writable_bytes(buf));
        co_await socket.async_send_to(
            coio::as_bytes(buf, n), peer);
    }
}
```

## See Also

- [basic_socket](socket.md)
- [basic_datagram_socket](socket.md)
- [basic_resolver](resolver.md)
