#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h> // IWYU pragma: keep
#include <Windows.h>
#include <system_error>
#include <variant>
#include <coio/detail/error.h> // IWYU pragma: keep
#include <coio/detail/config.h>

namespace coio {
    class endpoint;
    class ipv4_address;
    class ipv6_address;
}

namespace coio::detail {
    COIO_ALWAYS_INLINE auto to_error_code(::DWORD err) noexcept -> std::error_code {
        return {static_cast<int>(err), std::system_category()};
    }

    COIO_ALWAYS_INLINE auto throw_win_error(BOOL ok, const char* msg = nullptr) -> void {
        if (ok) [[likely]] return;
        const DWORD err = ::GetLastError();
        if (msg) throw std::system_error(static_cast<int>(err), std::system_category(), msg);
        throw std::system_error(static_cast<int>(err), std::system_category());
    }

    COIO_ALWAYS_INLINE auto throw_wsa_error(int rc, const char* msg = nullptr) -> void {
        if (rc != SOCKET_ERROR) [[likely]] return;
        int err = ::WSAGetLastError();
        if (err == ERROR_NETNAME_DELETED) err = WSAECONNRESET;
        else if (err == ERROR_PORT_UNREACHABLE) err = WSAECONNREFUSED;
        if (msg) throw std::system_error(err, std::system_category(), msg);
        throw std::system_error(err, std::system_category());
    }

    auto endpoint_to_sockaddr_in(const endpoint& addr) noexcept -> std::variant<::sockaddr_in, ::sockaddr_in6>;

    auto sockaddr_to_endpoint(::sockaddr* sa) noexcept -> endpoint;

    auto sockaddr_storage_to_endpoint(::sockaddr_storage& addr) noexcept -> endpoint;

    auto to_sockaddr(std::variant<::sockaddr_in, ::sockaddr_in6>& sa) noexcept -> std::pair<::sockaddr*, int>;
}
