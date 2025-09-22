#pragma once
#include <cstdio>
#include <cerrno>
#include <exception>
#include <variant>
#include <system_error>
#include <netinet/in.h>
#include <sys/socket.h>
#include "../../error.h"

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

    COIO_ALWAYS_INLINE auto is_blocking_errno(int errno_) noexcept ->bool {
#if EWOULDBLOCK == EAGAIN
        return errno_ == EWOULDBLOCK;
#else
        return errno_ == EWOULDBLOCK or errno_ == EAGAIN;
#endif
    }

    [[nodiscard]]
    COIO_ALWAYS_INLINE auto make_system_error_from_nonblock_errno(const char* msg = nullptr) noexcept -> std::exception_ptr {
        int errno_ = errno;
        if (is_blocking_errno(errno_)) return {};
        return std::make_exception_ptr(
            msg ?
            std::system_error(errno_, std::system_category(), msg) :
            std::system_error(errno_, std::system_category())
        );
    }

    auto endpoint_to_sockaddr_in(const endpoint& addr) noexcept -> std::variant<::sockaddr_in, ::sockaddr_in6>;

    auto sockaddr_to_endpoint(::sockaddr* sa) noexcept -> endpoint;

    auto sockaddr_storage_to_endpoint(::sockaddr_storage& addr) noexcept -> endpoint ;

    auto to_sockaddr(std::variant<::sockaddr_in, ::sockaddr_in6>& sa)-> std::pair<::sockaddr*, ::socklen_t> ;

    COIO_ALWAYS_INLINE auto make_eof_error(const char* msg = nullptr) -> std::system_error {
        return msg ? std::system_error{error::eof, msg} : std::system_error{error::eof};
    }
}