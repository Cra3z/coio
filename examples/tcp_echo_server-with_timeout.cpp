#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/utils/signal_wait.h>
#include "common.h"

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

auto with_timeout(coio::execution::sender auto sndr, io_context::scheduler sched, std::chrono::milliseconds ms) {
    return coio::when_any(
        std::move(sndr),
        sched.schedule_after(ms) | coio::let_value([]() noexcept { return coio::just_stopped(); })
    );
}

auto handle_connection(tcp_socket socket) -> io_context::task<> {
    using namespace std::chrono_literals;
    auto remote_endpoint = socket.remote_endpoint();
    ::debug("new connection from [{}]", remote_endpoint);
    io_context::scheduler sched = co_await coio::read_scheduler();
    try {
        char buffer[1024];
        while (true) {
            const auto length = co_await with_timeout(
                socket.async_read_some(coio::as_writable_bytes(buffer)),
                sched,
                3s
            );
            ::debug("{}", std::string_view{buffer, length});
            co_await (coio::async_write(socket, coio::as_bytes(buffer, length)) | as_throwing);
        }
    }
    catch (const std::system_error& e) {
        ::debug("connection with [{}] broken because \"{}\"", remote_endpoint, e.what());
    }
}

auto start_server(coio::async_scope& scope) -> io_context::task<> try {
    io_context::scheduler sched = co_await coio::read_scheduler();
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    ::debug("server \"{}\" start...", acceptor.local_endpoint());
    while (true) {
        scope.spawn_on(sched, handle_connection(co_await acceptor.async_accept()));
    }
}
catch (const std::system_error& e) {
    ::println("acceptor error: {}", e.what());
}

auto signal_watchdog(io_context& context) -> coio::inline_task<> {
    const int signum = co_await coio::signal_wait(SIGINT, SIGTERM);
    ::debug("server stop with signal: ({}){}", signum, coio::strsignal(signum));
    context.request_stop();
}

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn_on(context.get_scheduler(), start_server(scope));
    scope.spawn(signal_watchdog(context));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
