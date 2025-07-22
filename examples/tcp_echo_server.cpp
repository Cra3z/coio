#include <coio/core.h>
#include <coio/async_io.h>
#include <coio/net/socket.h>
#include "common.h"

auto handle_connection(coio::tcp_socket socket) -> coio::task<> {
    auto remote_endpoint = socket.remote_endpoint();
    try {
        std::uint8_t length;
        char buffer[255];
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
        ::println("connection with [{}] broken because \"{}\"", remote_endpoint, e.what());
    }
}

auto main() -> int {
    coio::io_context context;
    coio::sync_wait(coio::when_all(
        [&context]() -> coio::task<> {
            coio::async_scope scope;
            coio::tcp_acceptor acceptor{context, {coio::ipv4_address::any(), 8086}};
            ::println("server \"{}\" start...", acceptor.local_endpoint());
            while (true) {
                coio::tcp_socket socket = co_await acceptor.async_accept();
                scope.spawn(handle_connection(std::move(socket)));
            }
            co_await scope;
        }(),
        [&context]() -> coio::task<> {
            co_return context.run();
        }()
    ));
}