#include <coio/net/socket.h>
#include <coio/utils/utility.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep
#include "../common.h"

namespace coio::detail::socket {
    namespace {
        auto check_handle(socket_native_handle_type handle) -> void {
            if (handle == invalid_socket_handle) [[unlikely]] {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
            }
        }

        auto poll_socket(socket_native_handle_type handle, short kind, const char* msg) -> void {
            while (true) {
                ::fd_set read_set;
                ::fd_set write_set;
                ::fd_set except_set;
                FD_ZERO(&read_set);
                FD_ZERO(&write_set);
                FD_ZERO(&except_set);

                ::fd_set* readfds = nullptr;
                ::fd_set* writefds = nullptr;
                ::fd_set* exceptfds = nullptr;
                switch (kind) {
                case 0: // recv/recvfrom
                    FD_SET(handle, &read_set);
                    readfds = &read_set;
                    break;
                case 1: // send/sendto
                    FD_SET(handle, &write_set);
                    writefds = &write_set;
                    break;
                case 2: // connect
                    FD_SET(handle, &write_set);
                    FD_SET(handle, &except_set);
                    writefds = &write_set;
                    exceptfds = &except_set;
                    break;
                default:
                    unreachable();
                }

                if (::select(0, readfds, writefds, exceptfds, nullptr) == SOCKET_ERROR) {
                    const int err = ::WSAGetLastError();
                    if (err == WSAEINTR) continue;
                    throw std::system_error{err, std::system_category(), msg};
                }
                return;
            }
        }

        COIO_ALWAYS_INLINE auto poll_read(socket_native_handle_type handle, const char* msg) -> void {
            poll_socket(handle, 0, msg);
        }

        COIO_ALWAYS_INLINE auto poll_write(socket_native_handle_type handle, const char* msg) -> void {
            poll_socket(handle, 1, msg);
        }

        COIO_ALWAYS_INLINE auto poll_connect(socket_native_handle_type handle, const char* msg) -> void {
            poll_socket(handle, 2, msg);
        }

        [[noreturn]]
        COIO_ALWAYS_INLINE auto throw_wsa_error_value(int err, const char* msg) -> void {
            if (err == ERROR_NETNAME_DELETED) err = WSAECONNRESET;
            else if (err == ERROR_PORT_UNREACHABLE) err = WSAECONNREFUSED;
            throw std::system_error{err, std::system_category(), msg};
        }
    }

    auto local_endpoint(socket_native_handle_type handle) -> endpoint {
        check_handle(handle);
        ::sockaddr_storage addr{};
        int len = sizeof(addr);
        const int rc = ::getsockname(
            handle,
            reinterpret_cast<::sockaddr*>(&addr), &len
        );
        throw_wsa_error(rc);
        return sockaddr_storage_to_endpoint(addr);
    }

    auto remote_endpoint(socket_native_handle_type handle) -> endpoint {
        check_handle(handle);
        ::sockaddr_storage addr{};
        int len = sizeof(addr);
        const int rc = ::getpeername(
            handle,
            reinterpret_cast<::sockaddr*>(&addr), &len
        );
        throw_wsa_error(rc);
        return sockaddr_storage_to_endpoint(addr);
    }

    auto shutdown(socket_native_handle_type handle, shutdown_type how) -> void {
        check_handle(handle);
        int h;
        using enum shutdown_type;
        switch (how) {
        case shutdown_receive: h = SD_RECEIVE;
            break;
        case shutdown_send: h = SD_SEND;
            break;
        case shutdown_both: h = SD_BOTH;
            break;
        default: unreachable();
        }
        throw_wsa_error(::shutdown(handle, h));
    }

    auto bind(socket_native_handle_type handle, const endpoint& local_endpoint) -> void {
        check_handle(handle);
        auto sa = endpoint_to_sockaddr_in(local_endpoint);
        auto [psa, len] = to_sockaddr(sa);
        throw_wsa_error(::bind(handle, psa, len), "bind");
    }

    auto set_sockopt(socket_native_handle_type handle, int level, int option_name, std::span<const std::byte> value) -> void {
        check_handle(handle);
        throw_wsa_error(
           ::setsockopt(handle, level, option_name, reinterpret_cast<const char*>(value.data()), static_cast<int>(value.size())),
            "basic_socket::set_option"
        );
    }

    auto get_sockopt(socket_native_handle_type handle, int level, int option_name, std::span<std::byte> value) -> void {
        check_handle(handle);
        int n = static_cast<int>(value.size());
        throw_wsa_error(
            ::getsockopt(handle, level, option_name, reinterpret_cast<char*>(value.data()), &n),
            "basic_socket::get_option"
        );
        COIO_ASSERT(n == static_cast<int>(value.size()));
    }

    auto sol_socket_v() noexcept -> int {
        return SOL_SOCKET;
    }

    auto ipproto_ipv6_v() noexcept -> int {
        return IPPROTO_IPV6;
    }

    auto ipproto_tcp_v() noexcept -> int {
        return IPPROTO_TCP;
    }

    auto sock_option_traits<::linger>::from_value(const ::linger& value) noexcept -> linger_storage {
        return std::bit_cast<linger_storage>(value);
    }

    auto sock_option_traits<::linger>::to_value(const linger_storage& storage) noexcept -> ::linger {
        return std::bit_cast<::linger>(storage);
    }

    auto debug::name() noexcept -> int {
        return SO_DEBUG;
    }

    auto do_not_route::name() noexcept -> int {
        return SO_DONTROUTE;
    }

    auto broadcast::name() noexcept -> int {
        return SO_BROADCAST;
    }

    auto keep_alive::name() noexcept -> int {
        return SO_KEEPALIVE;
    }

    auto linger::name() noexcept -> int {
        return SO_LINGER;
    }

    auto out_of_band_inline::name() noexcept -> int {
        return SO_OOBINLINE;
    }

    auto receive_buffer_size::name() noexcept -> int {
        return SO_RCVBUF;
    }

    auto receive_low_watermark::name() noexcept -> int {
        return SO_RCVLOWAT;
    }

    auto reuse_address::name() noexcept -> int {
        return SO_REUSEADDR;
    }

    auto send_buffer_size::name() noexcept -> int {
        return SO_SNDBUF;
    }

    auto send_low_watermark::name() noexcept -> int {
        return SO_SNDLOWAT;
    }

    auto v6_only::name() noexcept -> int {
        return IPV6_V6ONLY;
    }

    auto no_delay::name() noexcept -> int {
        return TCP_NODELAY;
    }

    auto open(int family, int type, int protocol_id) -> socket_native_handle_type {
        const ::SOCKET handle = ::WSASocketW(family, type, protocol_id, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (handle == static_cast<::SOCKET>(SOCKET_ERROR)) [[unlikely]] {
            throw std::system_error{to_error_code(static_cast<::DWORD>(::WSAGetLastError())), "open"};
        }
        return handle;
    }

    auto close(socket_native_handle_type handle) -> void {
        if (handle == invalid_socket_handle) return;
        throw_wsa_error(::closesocket(handle), "close");
    }

    auto max_backlog() noexcept -> std::size_t {
        return SOMAXCONN;
    }

    auto listen(socket_native_handle_type handle, std::size_t backlog) -> void {
        check_handle(handle);
        if (backlog > INT_MAX) throw std::system_error{std::make_error_code(std::errc::value_too_large), "listen"};
        throw_wsa_error(::listen(handle, static_cast<int>(backlog)), "listen");
    }

    auto connect(socket_native_handle_type handle, const endpoint& peer) -> void {
        check_handle(handle);
        auto sa = endpoint_to_sockaddr_in(peer);
        auto [psa, len] = to_sockaddr(sa);
        if (::connect(handle, psa, len) == 0) return;
        const int err = ::WSAGetLastError();
        if (err != WSAEWOULDBLOCK and err != WSAEINPROGRESS) {
            throw_wsa_error_value(err, "connect");
        }

        poll_connect(handle, "connect");

        int so_error = 0;
        int so_error_len = sizeof(so_error);
        throw_wsa_error(::getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &so_error_len), "connect");
        if (so_error != 0) {
            throw_wsa_error_value(so_error, "connect");
        }
    }

    auto accept(socket_native_handle_type handle) -> socket_native_handle_type {
        check_handle(handle);
        while (true) {
            const ::SOCKET accepted = ::accept(handle, nullptr, nullptr);
            if (accepted == static_cast<::SOCKET>(SOCKET_ERROR)) [[unlikely]] {
                const int err = ::WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    poll_read(handle, "accept");
                    continue;
                }
                throw_wsa_error_value(err, "accept");
            }
            return accepted;
        }
    }

    auto receive(socket_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t {
        check_handle(handle);
        while (true) {
            const int n = ::recv(
                handle,
                reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(buffer.size()), 0
            );
            if (n == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    poll_read(handle, "receive");
                    continue;
                }
                throw_wsa_error_value(err, "receive");
            }
            return static_cast<std::size_t>(n);
        }
    }

    auto send(socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
        check_handle(handle);
        while (true) {
            const int n = ::send(
                handle,
                reinterpret_cast<const char*>(buffer.data()),
                static_cast<int>(buffer.size()), 0
            );
            if (n == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    poll_write(handle, "send");
                    continue;
                }
                throw_wsa_error_value(err, "send");
            }
            return static_cast<std::size_t>(n);
        }
    }

    auto receive_from(socket_native_handle_type handle, std::span<std::byte> buffer) -> std::pair<endpoint, size_t> {
        check_handle(handle);
        while (true) {
            ::sockaddr_storage addr{};
            int len = sizeof(addr);
            const int n = ::recvfrom(
                handle,
                reinterpret_cast<char*>(buffer.data()), static_cast<int>(std::min<std::size_t>(buffer.size(), INT_MAX)),
                0, reinterpret_cast<::sockaddr*>(&addr), &len
            );
            if (n == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    poll_read(handle, "receive_from");
                    continue;
                }
                throw_wsa_error_value(err, "receive_from");
            }
            return {sockaddr_storage_to_endpoint(addr), static_cast<std::size_t>(n)};
        }
    }

    auto send_to(socket_native_handle_type handle, std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t {
        check_handle(handle);
        auto sa = endpoint_to_sockaddr_in(dest);
        auto [psa, len] = to_sockaddr(sa);
        while (true) {
            const int n = ::sendto(
                handle,
                reinterpret_cast<const char*>(buffer.data()), static_cast<int>(std::min<std::size_t>(buffer.size(), INT_MAX)),
                0, psa, len
            );
            if (n == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    poll_write(handle, "send_to");
                    continue;
                }
                throw_wsa_error_value(err, "send_to");
            }
            return static_cast<std::size_t>(n);
        }
    }
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
