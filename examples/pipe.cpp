#include <thread>
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/epoll_context.h>
#include <coio/asyncio/pipe.h>
#include "common.h"

#if COIO_OS_LINUX
using io_context = coio::epoll_context;
#endif

auto main() -> int {
    io_context context;
    auto [reader, writer] = coio::make_pipe(context.get_scheduler());
    coio::async_scope scope;
    scope.spawn([](auto r) -> coio::task<> {
        try {
            char buffer[128];
            while (true) {
                auto n = co_await r.async_read_some(coio::as_writable_bytes(buffer));
                std::string_view message{buffer, n};
                if (message.ends_with('\xff')) break;
                std::clog << message;
            }
        }
        catch (std::system_error& e) {
            if (e.code() != coio::error::eof) {
                ::println("connection broken because of {}", e.what());
            }
        }
    }(std::move(reader)));

    scope.spawn([](auto w) -> coio::task<> {
        std::string_view messages[]{
          "Lorem ipsum dolor sit amet, consectetur adipiscing elit",
          "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.",
          "Ut enim ad minim veniam, quis nostrud exercitation ullamco",
          "laboris nisi ut aliquip ex ea commodo consequat.",
          "Duis aute irure dolor in reprehenderit in voluptate velit esse",
          "cillum dolore eu fugiat nulla pariatur.",
          "Excepteur sint occaecat cupidatat non proident",
          "sunt in culpa qui officia deserunt mollit anim id est laborum.",
          "\xff"
        };
        for (std::string_view message : messages) {
            co_await coio::async_write(w, coio::as_bytes(message));
        }
    }(std::move(writer)));

    context.run();

    coio::sync_wait(scope.join());
}