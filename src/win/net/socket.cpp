#include <memory>
#include <coio/net/socket.h>
#include <coio/utils/utility.h>
#include "../common.h"

namespace coio::detail::socket {
    namespace {
        auto check_handle(socket_native_handle_type handle) -> void {
            if (handle == invalid_socket_handle) [[unlikely]] {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
            }
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
           ::setsockopt(handle, level, option_name, reinterpret_cast<const char*>(value.data()), value.size()),
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
        COIO_ASSERT(n == value.size());
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

    auto no_dely::name() noexcept -> int {
        return TCP_NODELAY;
    }

    auto open(int family, int type, int protocol_id) -> socket_native_handle_type {
        const ::SOCKET handle = ::WSASocketW(family, type, protocol_id, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (handle == SOCKET_ERROR) [[unlikely]] {
            throw std::system_error{to_error_code(::WSAGetLastError()), "open"};
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
        throw_wsa_error(::connect(handle, psa, len), "connect");
    }

    auto accept(socket_native_handle_type handle) -> socket_native_handle_type {
        check_handle(handle);
        const ::SOCKET accepted = ::accept(handle, nullptr, nullptr);
        if (accepted == SOCKET_ERROR) [[unlikely]] {
            throw std::system_error{to_error_code(::WSAGetLastError()), "accept"};
        }
        return accepted;
    }

    auto receive(socket_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t {
        check_handle(handle);
        const int n = ::recv(
            handle,
            reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()), 0
        );
        throw_wsa_error(n, "receive");
        return static_cast<std::size_t>(n);
    }

    auto send(socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
        check_handle(handle);
        const int n = ::send(
            handle,
            reinterpret_cast<const char*>(buffer.data()),
            static_cast<int>(buffer.size()), 0
        );
        throw_wsa_error(n, "send");
        return static_cast<std::size_t>(n);
    }

    auto receive_from(socket_native_handle_type handle, std::span<std::byte> buffer) -> std::pair<endpoint, size_t> {
        check_handle(handle);
        ::sockaddr_storage addr{};
        int len = sizeof(addr);
        const int n = ::recvfrom(
            handle,
            reinterpret_cast<char*>(buffer.data()), static_cast<int>(std::min<std::size_t>(buffer.size(), INT_MAX)),
            0, reinterpret_cast<::sockaddr*>(&addr), &len
        );
        throw_wsa_error(n, "receive_from");
        return {sockaddr_storage_to_endpoint(addr), static_cast<std::size_t>(n)};
    }

    auto send_to(socket_native_handle_type handle, std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t {
        check_handle(handle);
        auto sa = endpoint_to_sockaddr_in(dest);
        auto [psa, len] = to_sockaddr(sa);
        const int n = ::sendto(
            handle,
            reinterpret_cast<const char*>(buffer.data()), static_cast<int>(std::min<std::size_t>(buffer.size(), INT_MAX)),
            0, psa, len
        );
        throw_wsa_error(n, "send_to");
        return static_cast<std::size_t>(n);
    }
}