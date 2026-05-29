#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/file.h>
#include "common.h"

#if COIO_OS_LINUX
#include <coio/asyncio/uring_context.h>
using io_context = coio::uring_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif
using stream_file = coio::stream_file<io_context::scheduler>;

auto cat(coio::zstring_view path) -> io_context::task<> {
    try {
        stream_file file{co_await coio::execution::read_env(coio::execution::get_scheduler), path, stream_file::read_only};
        ::println("this file has {} byte(s)", file.size());
        char buffer[1024];
        while (true) {
            const auto n = co_await file.async_read_some(coio::as_writable_bytes(buffer));
            ::print("{}", std::string_view{buffer, n});
        }
    }
    catch (std::system_error& e) {
        if (e.code() == coio::error::eof) {
            co_return;
        }
        ::println("[FATAL] {}", e.what());
    }
    catch (std::exception& e) {
        ::println("[FATAL] {}", e.what());
    }
}

auto main(int argc, char** argv) -> int {
    if (argc != 2) {
        ::println("Usage: {} <file-path>", argv[0]);
        return EXIT_FAILURE;
    }
    io_context context;
    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(context.get_scheduler(), cat(argv[1])),
        [&]() -> coio::task<> {
            context.run(); co_return;
        }()
    ));
}
