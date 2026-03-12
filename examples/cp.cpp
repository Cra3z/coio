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

using random_access_file = coio::random_access_file<io_context::scheduler>;

constexpr std::size_t block_size = 1024;

auto async_copy_file(io_context& context, coio::zstring_view src, coio::zstring_view dst) -> coio::task<> {
    random_access_file src_file{
        context.get_scheduler(),
        src,
        random_access_file::read_only
    };

    random_access_file dst_file{
        context.get_scheduler(),
        dst,
        random_access_file::write_only | random_access_file::create | random_access_file::truncate
    };

    const std::size_t total_size = src_file.size();
    dst_file.resize(total_size);

    try {
        std::byte buffer[block_size];
        std::size_t offset = 0;
        while (offset < total_size) {
            const auto n = co_await src_file.async_read_some_at(offset, coio::as_writable_bytes(buffer));
            co_await coio::async_write_at(dst_file, offset, coio::as_bytes(buffer, n));
            offset += n;
        }
    }
    catch (const std::system_error& e) {
        if (e.code() != coio::error::eof) throw;
    }

    dst_file.sync_all();
}

auto main(int argc, char** argv) -> int try {
    if (argc != 3) {
        ::println("  {} <src> <dst>", argv[0]);
        return EXIT_FAILURE;
    }

    io_context context;
    coio::this_thread::sync_wait(coio::when_all(
        async_copy_file(context, argv[1], argv[2]),
        [&]() -> coio::task<> {
            context.run();
            co_return;
        }()
    ));
}
catch (const std::exception& e) {
    ::println("[FATAL] {}", e.what());
    return EXIT_FAILURE;
}