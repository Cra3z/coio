#include <atomic>
#include <format>
#include <coio/asyncio/pipe.h>
#include "../common.h"

namespace coio::detail {
    auto make_native_pipe() -> std::pair<file_native_handle_type, file_native_handle_type> {
        static std::atomic<std::size_t> index{0};
        const std::size_t pid = ::GetCurrentProcessId();
        const std::string name = std::format(R"(\\.\pipe\coio_{}_{})", pid, index.fetch_add(1, std::memory_order_relaxed));
        const auto reader = ::CreateNamedPipeA(
            name.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            nullptr
        );
        if (reader == INVALID_HANDLE_VALUE) {
            throw std::system_error{to_error_code(::GetLastError()), "make_pipe"};
        }

        const auto writer = ::CreateFileA(
            name.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr
        );
        if (writer == INVALID_HANDLE_VALUE) {
            const ::DWORD err = ::GetLastError();
            ::CloseHandle(reader);
            throw std::system_error{to_error_code(err), "make_pipe"};
        }

        return {reader, writer};
    }
}