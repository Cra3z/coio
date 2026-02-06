#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/epoll_context.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/utils/flat_buffer.h>
#include "common.h"

#if COIO_OS_LINUX
using io_context = coio::epoll_context;
#endif
using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

auto main() -> int {
    io_context context;
    coio::async_scope scope;
    scope.spawn([](io_context::scheduler sched) -> coio::task<> {
        try {
            tcp_socket socket{sched};
            co_await socket.async_connect({coio::ipv4_address::loopback(), 8086});
            ::println("input messages to send to echo server (type 'exit' or 'quit' to quit):");
            while (true) {
                ::print(">> ");
                std::string content;
                std::getline(std::cin, content);
                if (content.empty()) continue;
                if (content == "exit" or content == "quit") break;
                co_await coio::async_write(socket, coio::as_bytes(content));
                std::size_t content_length = content.size();
                coio::flat_buffer buffer;
                co_await coio::async_read(socket, buffer, content_length);
                const auto buffer_data = buffer.data();
                ::println("-- {}", std::string_view{reinterpret_cast<const char*>(buffer_data.data()), buffer_data.size()});
                buffer.consume(content_length);
            }
        }
        catch (const std::exception& e) {
            println("error: {}", e.what());
        }
    }(context.get_scheduler()));
    context.run();
    coio::this_thread::sync_wait(scope.join());
}