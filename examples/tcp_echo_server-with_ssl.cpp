#include <array>
#include <filesystem>
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/tcp.h>
#include <coio/ssl/stream.h>
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

auto handle_client(tcp_socket socket, coio::ssl::context& ssl_ctx) -> coio::task<> try {
    ssl_stream stream{std::move(socket), ssl_ctx};
    co_await stream.async_handshake(coio::ssl::handshake_type::server);

    std::array<char, 4096> buffer{};
    while (true) {
        const auto bytes = co_await stream.async_read_some(coio::as_writable_bytes(buffer));
        co_await coio::async_write(stream, coio::as_bytes(std::span{buffer.data(), bytes}));
    }
}
catch (const std::system_error& e) {
    if (e.code() != coio::error::eof) {
        println("ssl server session error: {}", e.what());
    }
}
catch (const std::exception& e) {
    println("ssl server session error: {}", e.what());
}

auto serve(io_context::scheduler sched, coio::async_scope& client_scope, std::string cert, std::string key) -> coio::task<> try {
    coio::ssl::context ssl_ctx{coio::ssl::method::tls_server};
    // ssl_ctx.add_verify_path(std::filesystem::path{cert}.parent_path().c_str());
    ssl_ctx.use_certificate_file(cert);
    ssl_ctx.use_private_key_file(key);
    ssl_ctx.check_private_key();

    tcp_acceptor acceptor{sched, {coio::ipv4_address::loopback(), 8443}};
    println("ssl echo server listening on 127.0.0.1:8443");
    while (true) {
        auto socket = co_await acceptor.async_accept();
        client_scope.spawn(handle_client(std::move(socket), ssl_ctx));
    }
}
catch (const std::exception& e) {
    println("error: {}", e.what());
}

auto main(int argc, char** argv) -> int {
    if (argc < 3) {
        println(std::cerr, "usage: ssl_echo_server <cert.pem> <key.pem>");
        return 1;
    }

    io_context context;
    coio::async_scope scope;
    scope.spawn(serve(context.get_scheduler(), scope, argv[1], argv[2]));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
