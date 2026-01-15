#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/epoll_context.h>
#include <coio/asyncio/uring_context.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/utils/signal_set.h>
#include "common.h"

#if COIO_OS_LINUX
using io_context = coio::uring_context;
#endif
using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

auto handle_connection(tcp_socket socket, coio::inplace_stop_token stop_token) -> coio::task<> {
    auto remote_endpoint = socket.remote_endpoint();
    try {
        char buffer[1024];
        while (not stop_token.stop_requested()) {
            const auto length = co_await socket.async_read_some(coio::as_writable_bytes(buffer), stop_token);
            co_await coio::async_write(socket, coio::as_bytes(buffer, length), stop_token);
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
    auto stop_token = sched.context().get_stop_token();
    try {
        while (not stop_token.stop_requested()) {
            tcp_socket socket = co_await acceptor.async_accept(stop_token);
            scope.spawn(handle_connection(std::move(socket), stop_token));
        }
    }
    catch (const std::system_error& e) {
        ::println("acceptor error: {}", e.what());
    }
    co_await scope.join();
}

auto signal_watchdog(io_context& context) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::println("server stop with signal: ({}){}", signum, ::strsignal(signum));
    context.request_stop();
}

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn(start_server(context.get_scheduler()));
    scope.spawn(signal_watchdog(context));
    context.run();
    coio::sync_wait(scope.join());
}