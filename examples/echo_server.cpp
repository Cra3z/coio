#include <coio/core.h>
#include <coio/net/socket.h>
#include "common.h"

auto handle_connection(coio::net::tcp_socket socket) -> coio::task<> try {
    std::uint8_t length;
    char buffer[255];
    auto remote_endpoint = socket.remote_endpoint();
    ::println("new connection with [{}]", remote_endpoint);
    while (true) {
        co_await coio::async_read(socket, std::as_writable_bytes(std::span{&length, 1}));
        co_await coio::async_read(socket, std::as_writable_bytes(std::span{buffer, length}));
        ::println("[{}] {}", remote_endpoint, std::string_view{buffer, length});
        for (std::size_t i = 0; i < length; ++i) buffer[i] = char(std::toupper(buffer[i]));
        co_await coio::async_write(socket, std::as_bytes(std::span{&length, 1}));
        co_await coio::async_write(socket, std::as_bytes(std::span{buffer, length}));
    }
}
catch (const std::system_error& e) {
    ::println("connection with [{}] broken because \"{}\"", socket.remote_endpoint(), e.what());
}

auto main() -> int {
    coio::io_context context;
    coio::sync_wait(coio::when_all(
        [&context]() -> coio::task<> {
            coio::async_scope scope;
            coio::net::tcp_acceptor acceptor{context, {coio::net::ipv4_address::any(), 8086}};
            ::println("server \"{}\" start...", acceptor.local_endpoint());
            try {
                while (true) {
                    coio::net::tcp_socket socket{context};
                    co_await acceptor.async_accept(socket);
                    scope.spawn(handle_connection(std::move(socket)));
                }
            }
            catch (const coio::operation_stopped&) {}
            co_await scope;
        }(),
        [&context]() -> coio::task<> {
            co_return context.run();
        }()
    ));
}