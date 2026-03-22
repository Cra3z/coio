#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/ssl/stream.h>
#include <coio/utils/signal_set.h>
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
using ssl_stream = coio::ssl::stream<tcp_socket>;

auto handle_connection(ssl_stream stream) -> coio::task<> {
    co_await stream.async_handshake(coio::ssl::handshake_type::server);
    auto remote_endpoint = stream.lowest_layer().remote_endpoint();
    ::debug("new connection from [{}]", remote_endpoint);
    try {
        char buffer[1024];
        while (true) {
            const auto length = co_await stream.async_read_some(coio::as_writable_bytes(buffer));
            ::debug("{}", std::string_view{buffer, length});
            co_await coio::async_write(stream, coio::as_bytes(buffer, length));
        }
    }
    catch (const std::system_error& e) {
        ::debug("connection with [{}] broken because \"{}\"", remote_endpoint, e.what());
    }
}

auto start_server(io_context::scheduler sched, coio::async_scope& scope, coio::zstring_view cert, coio::zstring_view key) -> coio::task<> try {
    coio::ssl::context ssl_ctx{coio::ssl::method::tls_server};
    ssl_ctx.use_certificate_file(cert);
    ssl_ctx.use_private_key_file(key);
    ssl_ctx.check_private_key();
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8088}};
    ::debug("server \"{}\" start...", acceptor.local_endpoint());
    while (true) {
        scope.spawn(handle_connection(ssl_stream{co_await acceptor.async_accept(), ssl_ctx}));
    }
}
catch (const std::system_error& e) {
    ::println("acceptor error: {}", e.what());
}

auto signal_watchdog(io_context& context) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::debug("server stop with signal: ({}){}", signum, coio::strsignal(signum));
    context.request_stop();
}

auto main(int argc, char** argv) -> int {
    if (argc < 3) {
        ::println(std::cerr, "usage: {} <certificate-file> <private-key-file>", argv[0]);
        ::println(std::cerr, "for example: {} examples/resources/cert.pem examples/resources/key.pem", argv[0]);
        return EXIT_FAILURE;
    }
    io_context context;
    coio::async_scope scope;
    scope.spawn(start_server(context.get_scheduler(), scope, argv[1], argv[2]));
    scope.spawn(signal_watchdog(context));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}