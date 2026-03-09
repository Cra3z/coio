#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
#include <cstdio>
#include <exception>
#include <system_error>
#include <variant>
#include <coio/detail/error.h>
#include <coio/detail/config.h>

namespace coio {
    class endpoint;
    class ipv4_address;
    class ipv6_address;
}

namespace coio::detail {
    COIO_ALWAYS_INLINE auto to_error_code(::DWORD err = ::GetLastError()) noexcept -> std::error_code {
        return {static_cast<int>(err), std::system_category()};
    }

    COIO_ALWAYS_INLINE auto throw_win_error(BOOL ok, const char* msg = nullptr) -> void {
        if (ok) return;
        const DWORD err = ::GetLastError();
        if (msg) throw std::system_error(static_cast<int>(err), std::system_category(), msg);
        throw std::system_error(static_cast<int>(err), std::system_category());
    }

    COIO_ALWAYS_INLINE auto throw_wsa_error(int rc, const char* msg = nullptr) -> void {
        if (rc != SOCKET_ERROR) return;
        const int err = ::WSAGetLastError();
        if (msg) throw std::system_error(err, std::system_category(), msg);
        throw std::system_error(err, std::system_category());
    }

    COIO_ALWAYS_INLINE auto no_error_here(BOOL ok, const char* msg = nullptr) noexcept -> void {
        if (ok) return;
        if (msg) std::perror(msg);
        std::terminate();
    }

    // Convert our socket_native_handle_type (UINT_PTR) to Windows SOCKET.
    COIO_ALWAYS_INLINE auto to_socket(socket_native_handle_type h) noexcept -> SOCKET {
        return static_cast<SOCKET>(h);
    }

    // Overload: accept HANDLE (void*) from iocp_state_base_for::handle.
    COIO_ALWAYS_INLINE auto to_socket(HANDLE h) noexcept -> SOCKET {
        return static_cast<SOCKET>(reinterpret_cast<std::uintptr_t>(h));
    }

    // Convert a Windows SOCKET back to socket_native_handle_type (UINT_PTR).
    COIO_ALWAYS_INLINE auto from_socket(SOCKET s) noexcept -> socket_native_handle_type {
        return static_cast<socket_native_handle_type>(s);
    }

    // Fill a sockaddr variant from an endpoint.
    auto endpoint_to_sockaddr_in(const endpoint& ep) noexcept
        -> std::variant<::sockaddr_in, SOCKADDR_IN6>;

    // Extract an endpoint from a generic sockaddr pointer.
    auto sockaddr_to_endpoint(SOCKADDR* sa) noexcept -> endpoint;

    // Extract an endpoint from a SOCKADDR_STORAGE.
    auto sockaddr_storage_to_endpoint(SOCKADDR_STORAGE& addr) noexcept -> endpoint;

    // Return (sockaddr*, length) from the variant.
    auto to_sockaddr(std::variant<SOCKADDR_IN, SOCKADDR_IN6>& sa) noexcept
        -> std::pair<SOCKADDR*, int>;

    // Get AcceptEx function pointer (cached).
    auto get_acceptex_fn() noexcept -> LPFN_ACCEPTEX;

    // Get ConnectEx function pointer (cached).
    auto get_connectex_fn() noexcept -> LPFN_CONNECTEX;

    // Get GetAcceptExSockaddrs function pointer (cached).
    auto get_acceptex_sockaddrs_fn() noexcept -> LPFN_GETACCEPTEXSOCKADDRS;

    // WSAStartup RAII guard – call once at program init.
    struct wsa_init_guard {
        wsa_init_guard() noexcept {
            WSADATA data;
            ::WSAStartup(MAKEWORD(2, 2), &data);
        }
        ~wsa_init_guard() noexcept {
            ::WSACleanup();
        }
    };

    inline const wsa_init_guard g_wsa_init;
}
