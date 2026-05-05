# coio::tcp

**Header:** `<coio/net/tcp.h>`

The TCP protocol type. Used to create type-safe TCP socket, acceptor, and resolver instances.

## Member Functions

| Name | Description |
|------|-------------|
| `static v4() -> tcp` | Returns a TCP/IPv4 protocol object. |
| `static v6() -> tcp` | Returns a TCP/IPv6 protocol object. |
| `family() const -> int` | Returns the address family (`AF_INET` or `AF_INET6`). |
| `static type() -> int` | Returns `SOCK_STREAM`. |
| `static protocol_id() -> int` | Returns `IPPROTO_TCP`. |
| `operator==(tcp, tcp) = default` | Comparison. |

Note: constructing a `tcp` object directly is not possible; use `tcp::v4()` or `tcp::v6()`.

## Type Aliases

```cpp
template<io_scheduler IoScheduler>
using socket = basic_stream_socket<tcp, IoScheduler>;

template<io_scheduler IoScheduler>
using acceptor = basic_socket_acceptor<tcp, IoScheduler>;

template<scheduler Scheduler>
using resolver = basic_resolver<tcp, Scheduler>;
```

These aliases simplify the most common usage patterns:

```cpp
using io_context = coio::iocp_context; // or epoll_context / uring_context
using tcp_socket   = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;
using tcp_resolver = coio::tcp::resolver<io_context::scheduler>;
```

## Socket Option

| Option | Description |
|--------|-------------|
| `tcp::no_delay` | Controls `TCP_NODELAY`. Enable with `socket.set_option(tcp::no_delay{true})`. |

## Example

```cpp
#include <coio/net/tcp.h>
#include <coio/net/socket.h>
#include <coio/asyncio/io.h>

auto echo_server(coio::tcp::acceptor<MyScheduler>& acceptor) -> coio::task<> {
    while (true) {
        auto client = co_await acceptor.async_accept();
        // handle client...
    }
}

int main() {
    io_context ctx;
    coio::tcp::acceptor acceptor{ctx.get_scheduler(),
        coio::endpoint{coio::ipv4_address::any(), 8080}};
    acceptor.set_option(coio::tcp::no_delay{true});
    // ...
}
```

## See Also

- [basic_socket](socket.md)
- [basic_socket_acceptor](socket.md)
- [basic_stream_socket](socket.md)
- [basic_resolver](resolver.md)
