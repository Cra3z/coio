#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/resolver.h>
#include <coio/net/tcp.h>
#include <coio/ssl/stream.h>
#include <coio/utils/flat_buffer.h>
#include "common.h"

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_resolver = coio::tcp::resolver<io_context::scheduler>;
using ssl_stream = coio::ssl::stream<tcp_socket>;

auto run_client(io_context::scheduler sched, std::string host, std::string port, bool verify_peer, std::string ca_file = {}) -> coio::task<> try {
    tcp_resolver resolver{sched};
    auto results = co_await resolver.async_resolve({.host_name = host, .service_name = port});

    tcp_socket socket{sched};
    bool connected = false;
    for (auto&& result : results) {
        co_await socket.async_connect(result.endpoint);
        connected = true;
        break;
    }
    if (not connected) {
        throw std::system_error{coio::error::make_error_code(coio::error::not_found), "resolve"};
    }

    coio::ssl::context ssl_ctx{coio::ssl::method::tls_client};
    if (verify_peer) {
        ssl_ctx.set_verify_mode(coio::ssl::verify_mode::peer);
        if (not ca_file.empty())
            ssl_ctx.load_verify_file(ca_file);
        else
            ssl_ctx.set_default_verify_paths();
    }

    ssl_stream stream{std::move(socket), ssl_ctx};
    if (verify_peer) {
        stream.set_host_name(host.c_str());
    }
    else {
        stream.set_verify_mode(coio::ssl::verify_mode::none);
        stream.set_server_name(host.c_str());
    }
    co_await stream.async_handshake(coio::ssl::handshake_type::client);

    println("connected with TLS to {}:{}", host, port);
    println("input messages to send to ssl echo server (type 'exit' or 'quit' to quit):");
    while (true) {
        print(">> ");
        std::string content;
        std::getline(std::cin, content);
        if (content.empty()) continue;
        if (content == "exit" or content == "quit") break;

        co_await coio::async_write(stream, coio::as_bytes(content));
        coio::flat_buffer buffer;
        co_await coio::async_read(stream, buffer, content.size());
        const auto data = buffer.data();
        println("-- {}", std::string_view{reinterpret_cast<const char*>(data.data()), data.size()});
        buffer.consume(content.size());
    }

    co_await stream.async_shutdown();
}
catch (const std::exception& e) {
    println("error: {}", e.what());
}

auto main(int argc, char** argv) -> int {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    std::string port = argc > 2 ? argv[2] : "8443";
    // argv[3]: path to CA cert file, or "noverify" to skip verification
    std::string ca_or_flag = argc > 3 ? argv[3] : "";
    bool verify_peer = ca_or_flag != "noverify";
    std::string ca_file = (verify_peer and not ca_or_flag.empty()) ? ca_or_flag : std::string{};

    io_context context;
    coio::async_scope scope;
    scope.spawn(run_client(context.get_scheduler(), std::move(host), std::move(port), verify_peer, std::move(ca_file)));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}
