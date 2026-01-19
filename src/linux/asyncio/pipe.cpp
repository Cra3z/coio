#include <sys/fcntl.h>
#include <coio/asyncio/pipe.h>
#include "../common.h"

namespace coio::detail {
    auto make_native_pipe() -> std::pair<file_native_handle_type, file_native_handle_type> {
        int pipefd[2];
        throw_last_error(::pipe2(pipefd, O_CLOEXEC), "make_pipe");
        return {pipefd[0], pipefd[1]};
    }
}