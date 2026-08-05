#pragma once
#include <cstddef>
#include <cstdio>
#include <cerrno>
#include <exception>
#include <span>
#include <utility>
#include <variant>
#include <system_error>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h> // IWYU pragma: keep
#include <coio/detail/error.h> // IWYU pragma: keep

namespace coio {
    class endpoint;
    class ipv4_address;
    class ipv6_address;
}

namespace coio::detail {
    COIO_ALWAYS_INLINE auto throw_last_error(::ssize_t value, const char* msg = nullptr) -> void {
        // assume: msg is null or null-terminated.
        if (value != -1) return;
        if (msg == nullptr) throw std::system_error(errno, std::system_category());
        throw std::system_error(errno, std::system_category(), msg);
    }

    COIO_ALWAYS_INLINE auto no_errno_here(::ssize_t value, const char* msg = nullptr) -> void {
        // assume: msg is null or null-terminated.
        if (value != -1) return;
        std::perror(msg);
        std::terminate();
    }

    COIO_ALWAYS_INLINE constexpr auto is_blocking_errno(int errno_) noexcept ->bool {
#if EWOULDBLOCK == EAGAIN
        return errno_ == EWOULDBLOCK;
#else
        return errno_ == EWOULDBLOCK or errno_ == EAGAIN;
#endif
    }

    COIO_ALWAYS_INLINE auto poll_file(int fd, short events, const char* msg) -> void {
        ::pollfd pfd{
            .fd = fd,
            .events = events,
            .revents = 0
        };
        while (true) {
            const int rc = ::poll(&pfd, 1, -1);
            if (rc == -1 and errno == EINTR) continue;
            throw_last_error(rc, msg);
            return;
        }
    }

    // data-path sync ops (defined in net/socket.cpp and asyncio/file.cpp),
    // exposed publicly through the io_object member functions
    enum class seek_whence;

    auto file_read(int handle, std::span<std::byte> buffer) -> std::size_t;

    auto file_write(int handle, std::span<const std::byte> buffer) -> std::size_t;

    auto file_read_at(int handle, std::size_t offset, std::span<std::byte> buffer) -> std::size_t;

    auto file_write_at(int handle, std::size_t offset, std::span<const std::byte> buffer) -> std::size_t;

    auto file_seek(int handle, std::size_t offset, seek_whence whence) -> std::size_t;

    auto file_resize(int handle, std::size_t new_size) -> void;

    namespace socket {
        auto receive(int handle, std::span<std::byte> buffer) -> std::size_t;

        auto send(int handle, std::span<const std::byte> buffer) -> std::size_t;

        auto receive_from(int handle, std::span<std::byte> buffer) -> std::pair<endpoint, std::size_t>;

        auto send_to(int handle, std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t;
    }

    auto endpoint_to_sockaddr_in(const endpoint& addr) noexcept -> std::variant<::sockaddr_in, ::sockaddr_in6>;

    auto sockaddr_to_endpoint(::sockaddr* sa) noexcept -> endpoint;

    auto sockaddr_storage_to_endpoint(::sockaddr_storage& addr) noexcept -> endpoint;

    auto to_sockaddr(std::variant<::sockaddr_in, ::sockaddr_in6>& sa)-> std::pair<::sockaddr*, ::socklen_t>;
}
