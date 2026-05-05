# Networking Guide

This guide covers common networking patterns: TCP echo server, TCP client, UDP echo server, and resolver usage.

## TCP Echo Server (Single-threaded)

```cpp
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/tcp.h>
#include <coio/net/socket.h>
#include <coio/utils/signal_set.h>

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_socket   = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

auto handle_connection(tcp_socket socket) -> coio::task<> {
    auto remote = socket.remote_endpoint();
    std::println("New connection from {}", remote);
    try {
        char buf[1024];
        while (true) {
            auto n = co_await socket.async_read_some(
                coio::as_writable_bytes(buf));
            co_await coio::async_write(socket, coio::as_bytes(buf, n));
        }
    } catch (const std::system_error& e) {
        if (e.code() == coio::error::eof) {
            std::println("Connection closed by {}", remote);
        } else {
            std::println("Error: {}", e.what());
        }
    }
}

auto server(io_context::scheduler sched, coio::async_scope& scope)
    -> coio::task<> {
    tcp_acceptor acceptor{sched,
        coio::endpoint{coio::ipv4_address::any(), 8080}};
    std::println("Listening on {}", acceptor.local_endpoint());

    while (true) {
        auto client = co_await acceptor.async_accept();
        scope.spawn(handle_connection(std::move(client)));
    }
}

auto signal_watchdog(io_context& ctx) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    int sig = co_await signals.async_wait();
    std::println("Stopping on signal: {}", coio::strsignal(sig));
    ctx.request_stop();
}

int main() {
    io_context ctx;
    coio::async_scope scope;
    scope.spawn(server(ctx.get_scheduler(), scope));
    scope.spawn(signal_watchdog(ctx));
    ctx.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## TCP Client

```cpp
auto tcp_client(io_context::scheduler sched) -> coio::task<> {
    tcp_socket socket{sched, coio::tcp::v4()};

    // Connect to server
    co_await socket.async_connect(
        coio::endpoint{coio::ipv4_address{127, 0, 0, 1}, 8080});
    std::println("Connected to {}", socket.remote_endpoint());

    // Send request
    std::string_view request = "Hello, Server!\n";
    co_await coio::async_write(socket, coio::as_bytes(request));

    // Read response
    char buf[1024];
    coio::flat_buffer buffer;
    auto n = co_await coio::async_read_until(socket, buffer, '\n');
    auto data = buffer.data();
    std::println("Response: {}",
        std::string_view{reinterpret_cast<const char*>(data.data()), n});
}
```

## UDP Echo Server

```cpp
using udp_socket = coio::udp::socket<io_context::scheduler>;

auto udp_echo_server(io_context::scheduler sched) -> coio::task<> {
    udp_socket socket{sched, coio::udp::v4()};
    socket.bind(coio::endpoint{coio::ipv4_address::any(), 8080});

    std::println("UDP echo server on {}", socket.local_endpoint());

    char buf[1500];
    while (true) {
        auto [peer, n] = co_await socket.async_receive_from(
            coio::as_writable_bytes(buf));
        std::println("Received {} bytes from {}", n, peer);
        co_await socket.async_send_to(coio::as_bytes(buf, n), peer);
    }
}
```

## DNS Resolution

```cpp
auto resolve_host(io_context::scheduler sched) -> coio::task<> {
    coio::tcp::resolver resolver{sched};

    // Synchronous resolution (returns a generator)
    for (auto result : coio::tcp::resolver::resolve(
            coio::tcp::v4(), {"example.com", "80"})) {
        std::println("  {} -> {}", result.canonical_name, result.endpoint);
    }
}
```

## Concurrency Rules

- **One read + one write** can be outstanding simultaneously on a stream socket
- **Two reads** (or two writes) outstanding simultaneously is not allowed
- **Acceptor**: at most one outstanding `async_accept`
- Sockets are **not thread-safe** — serialize access if using multiple threads directly

### Correct

```cpp
char read_buf[1024];
std::string_view write_buf = "ping";

// One read and one write in parallel — OK
co_await coio::when_all(
    sock.async_read_some(coio::as_writable_bytes(read_buf)),
    coio::async_write(sock, coio::as_bytes(write_buf))
);
```

### Incorrect

```cpp
char buf1[1024];
char buf2[1024];

// Two reads in parallel — NOT OK
co_await coio::when_all(
    sock.async_read_some(coio::as_writable_bytes(buf1)),
    sock.async_read_some(coio::as_writable_bytes(buf2))
);
```

## EOF Handling

When a TCP peer disconnects, `async_read_some` completes exceptionally with `coio::error::misc_errc::eof`:

```cpp
char buf[1024];

try {
    auto n = co_await sock.async_read_some(coio::as_writable_bytes(buf));
} catch (const std::system_error& e) {
    if (e.code() == coio::error::eof) {
        // peer disconnected — expected
    }
}
```
