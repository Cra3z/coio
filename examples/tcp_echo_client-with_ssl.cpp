#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
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
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;
using ssl_stream = coio::ssl::stream<tcp_socket>;

auto handle_connection(io_context::scheduler sched) -> coio::task<> try {
    tcp_socket socket{sched};
    co_await socket.async_connect({coio::ipv4_address::loopback(), 8088});
    ::println("local endpoint: {}", socket.local_endpoint());
    coio::ssl::context ssl_ctx{coio::ssl::method::tls_client};
    ssl_stream stream{std::move(socket), ssl_ctx};
    stream.set_verify_mode(coio::ssl::verify_mode::none);
    stream.set_server_name("localhost");
    co_await stream.async_handshake(coio::ssl::handshake_type::client);
    ::println("input messages to send to echo server (type 'exit' or 'quit' to quit):");
    while (true) {
        ::print(">> ");
        std::string content;
        std::getline(std::cin, content);
        if (content.empty()) continue;
        if (content == "exit" or content == "quit") break;
        co_await coio::async_write(stream, coio::as_bytes(content));
        std::size_t content_length = content.size();
        coio::flat_buffer buffer;
        co_await coio::async_read(stream, buffer, content_length);
        const auto buffer_data = buffer.data();
        ::println("-- {}", std::string_view{reinterpret_cast<const char*>(buffer_data.data()), buffer_data.size()});
        buffer.consume(content_length);
    }
}
catch (const std::exception& e) {
    println("error: {}", e.what());
}

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn(handle_connection(context.get_scheduler()));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}