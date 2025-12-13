#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/epoll_context.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include "common.h"

#if COIO_OS_LINUX
using io_context = coio::epoll_context;
#endif
using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

auto handle_connection(tcp_socket socket) -> coio::task<> {
    auto remote_endpoint = socket.remote_endpoint();
    try {
        char buffer[255];
        ::println("new connection with [{}]", remote_endpoint);
        while (true) {
            const auto length = co_await socket.async_read_some(coio::as_writable_bytes(buffer));
            ::println("[{}] {}", remote_endpoint, std::string_view{buffer, length});
            co_await coio::async_write(socket, coio::as_bytes(buffer, length));
        }
    }
    catch (const std::system_error& e) {
        ::println("connection with [{}] broken because \"{}\"", remote_endpoint, e.what());
    }
}

auto start_server(io_context::scheduler sched) -> coio::task<> {
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    ::println("server \"{}\" start...", acceptor.local_endpoint());
    coio::async_scope scope;
    try {
        while (true) {
            tcp_socket socket = co_await acceptor.async_accept();
            scope.spawn(handle_connection(std::move(socket)));
        }
    }
    catch (const std::system_error& e) {
        ::println("acceptor error: {}", e.what());
    }
    co_await scope.join();
}

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn(start_server(context.get_scheduler()));
    context.run();
    coio::sync_wait(scope.join());
}