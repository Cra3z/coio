#include <atomic>
#include <coio/asyncio/pipe.h>
#include "../common.h"

namespace coio::detail {
    // Create an async-ready pipe pair using a named pipe with a unique name.
    // Anonymous Windows pipes do not support overlapped I/O, so we use a named
    // pipe with FILE_FLAG_OVERLAPPED on both ends.
    auto make_native_pipe() -> std::pair<file_native_handle_type, file_native_handle_type>
    {
        // Generate a unique pipe name.
        static std::atomic<std::uint32_t> s_counter{0};
        const DWORD pid = ::GetCurrentProcessId();
        const std::uint32_t cnt = s_counter.fetch_add(1, std::memory_order_relaxed);
        char name[64];
        std::snprintf(name, sizeof(name),
            "\\\\.\\pipe\\coio_%lu_%u", static_cast<unsigned long>(pid), cnt);

        // Server (read) end.
        const HANDLE read_end = ::CreateNamedPipeA(
            name,
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,        // max instances
            4096,     // output buffer
            4096,     // input buffer
            0,        // default timeout
            nullptr); // default security
        if (read_end == INVALID_HANDLE_VALUE) {
            throw std::system_error(to_error_code(::GetLastError()), "make_pipe: create server end");
        }

        // Client (write) end.
        const HANDLE write_end = ::CreateFileA(
            name,
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);
        if (write_end == INVALID_HANDLE_VALUE) {
            const DWORD err = ::GetLastError();
            ::CloseHandle(read_end);
            throw std::system_error(static_cast<int>(err),
                std::system_category(), "make_pipe: open client end");
        }

        return {read_end, write_end};
    }
} // namespace coio::detail
