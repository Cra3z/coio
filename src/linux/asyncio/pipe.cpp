#include <sys/fcntl.h>
#include <coio/asyncio/pipe.h>
#include "../common.h"

namespace coio::detail {
    auto make_native_pipe() -> std::pair<pipe_native_handle_type, pipe_native_handle_type> {
        int pipefd[2];
        throw_last_error(::pipe2(pipefd, O_CLOEXEC), "make_pipe");
        return {pipefd[0], pipefd[1]};
    }

    auto close_pipe(pipe_native_handle_type pipe) -> void {
        if (pipe == -1) return;
        throw_last_error(::close(pipe), "close");
    }

    auto pipe_read(pipe_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t {
        const auto n = ::read(handle, buffer.data(), buffer.size());
        throw_last_error(n, "read_some");
        return n;
    }

    auto pipe_write(pipe_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
        const auto n = ::write(handle, buffer.data(), buffer.size());
        throw_last_error(n, "write_some");
        return n;
    }
}