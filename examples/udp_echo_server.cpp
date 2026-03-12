#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/udp.h>
#include <coio/utils/signal_set.h>
#include "common.h"

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
#include <coio/asyncio/uring_context.h>
using io_context = coio::uring_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using udp_socket = coio::udp::socket<io_context::scheduler>;

auto start_server(io_context::scheduler sched) -> coio::task<> try {
    udp_socket socket{sched, coio::udp::v4()};
    socket.bind(coio::endpoint{coio::ipv4_address::any(), 8087});

    ::debug("UDP echo server \"{}\" started...", socket.local_endpoint());

    char buffer[1024];
    while (true) {
        const auto [remote_endpoint, length] = co_await socket.async_receive_from(coio::as_writable_bytes(buffer));
        co_await socket.async_send_to(coio::as_bytes(buffer, length), remote_endpoint);
    }
}
catch (const std::system_error& e) {
    ::println("server error: {}", e.what());
}

auto signal_watchdog(io_context& context) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::debug("server stop with signal: ({}){}", signum, coio::strsignal(signum));
    context.request_stop();
}

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn(start_server(context.get_scheduler()));
    scope.spawn(signal_watchdog(context));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}

