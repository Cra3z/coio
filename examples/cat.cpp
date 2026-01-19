#include <coio/core.h>
#if COIO_HAS_IO_URING
#include <coio/asyncio/io.h>
#include <coio/asyncio/file.h>
#include <coio/asyncio/uring_context.h>
#include "common.h"

#if COIO_OS_LINUX
using io_context = coio::uring_context;
#endif
using stream_file = coio::stream_file<io_context::scheduler>;

auto main(int argc, char** argv) -> int {
    if (argc != 2) {
        ::println("Usage: {} <file-path>", argv[0]);
        return EXIT_FAILURE;
    }
    io_context context;
    coio::sync_wait(coio::when_all(
        [&]() -> coio::task<> {
            try {
                stream_file file{context.get_scheduler(), argv[1], stream_file::read_only};
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
        }(),
        [&]() -> coio::task<> {
            context.run(); co_return;
        }()
    ));
}
#else
auto main() -> int { return EXIT_FAILURE; }
#endif