#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/udp.h>
#include "common.h"

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using udp_socket = coio::udp::socket<io_context::scheduler>;

auto handle_connection(io_context::scheduler sched) -> coio::task<> try {
    udp_socket socket{sched};
    socket.open(coio::udp::v4());

    coio::endpoint server_endpoint{coio::ipv4_address::loopback(), 8087};

    ::println("UDP echo client started");
    ::println("local endpoint: {}", socket.local_endpoint());
    ::println("input messages to send to echo server (type 'exit' or 'quit' to quit):");

    char buffer[1024];
    while (true) {
        ::print(">> ");
        std::string content;
        std::getline(std::cin, content);

        if (content.empty()) continue;
        if (content == "exit" or content == "quit") break;

        // Send message to server
        co_await socket.async_send_to(
            coio::as_bytes(content),
            server_endpoint
        );

        // Receive echo response
        coio::endpoint sender_endpoint;
        const auto length = co_await socket.async_receive_from(
            coio::as_writable_bytes(buffer),
            sender_endpoint
        );

        ::println("-- {}", std::string_view{buffer, length});
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

