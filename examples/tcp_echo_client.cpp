#include <coio/core.h>
#include <coio/async_io.h>
#include <coio/net/socket.h>
#include "common.h"

auto main() -> int {
    coio::io_context context;
    coio::sync_wait(coio::when_all(
        [&context]() -> coio::task<> {
            coio::tcp_socket socket{context};
            co_await socket.async_connect({coio::ipv4_address::loopback(), 8086});
            char buffer[256];
            std::uint8_t length;
            while (true) {
                ::print(">> ");
                std::cin.getline(buffer, std::ranges::size(buffer));
                if (not std::cin) break;
                auto input_length = std::cin.gcount() - 1;
                if (input_length == 0) continue;
                if (input_length > 255) {
                    ::println("message length can't be greater than 255, input again.");
                    continue;
                }
                length = input_length;
                if (std::string_view{buffer, length} == "exit") break;
                co_await coio::async_write(socket, std::as_bytes(std::span{&length, 1}));
                co_await coio::async_write(socket, std::as_bytes(std::span{buffer, length}));
                co_await coio::async_read(socket, std::as_writable_bytes(std::span{&length, 1}));
                co_await coio::async_read(socket, std::as_writable_bytes(std::span{buffer, length}));
                ::println("-- {}", std::string_view{buffer, length});
            }
        }(),
        [&context]() -> coio::task<> {
            co_return context.run();
        }()
    ));
}