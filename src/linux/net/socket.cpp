#include <cstdio>
#include <memory>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <coio/utils/utility.h>
#include <coio/net/socket.h>
#include "../common.h"

namespace coio::detail::socket {
    namespace {
        auto check_fd(socket_native_handle_type handle) -> void {
            if (handle == invalid_socket_handle) {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
            }
        }
    }

    auto local_endpoint(socket_native_handle_type handle) -> endpoint {
        check_fd(handle);
        ::sockaddr_storage addr{};
        ::socklen_t addr_len = sizeof(addr);
        const auto ret = ::getsockname(handle, reinterpret_cast<::sockaddr*>(&addr), &addr_len);
        throw_last_error(ret);
        return sockaddr_storage_to_endpoint(addr);
    }

    auto remote_endpoint(socket_native_handle_type handle) -> endpoint {
        check_fd(handle);
        ::sockaddr_storage addr{};
        ::socklen_t addr_len = sizeof(addr);
        const auto ret = ::getpeername(handle, reinterpret_cast<::sockaddr*>(&addr), &addr_len);
        throw_last_error(ret);
        return sockaddr_storage_to_endpoint(addr);
    }

    auto shutdown(socket_native_handle_type handle, shutdown_type how) -> void {
        check_fd(handle);
        int h;
        using enum shutdown_type;
        switch (how) {
        case shutdown_receive:
            h = SHUT_RD;
            break;
        case shutdown_send:
            h = SHUT_WR;
            break;
        case shutdown_both:
            h = SHUT_RDWR;
            break;
        default: unreachable();
        }
        throw_last_error(::shutdown(handle, h));
    }

    auto bind(socket_native_handle_type handle, const endpoint& local_endpoint) -> void {
        check_fd(handle);
        auto sa = endpoint_to_sockaddr_in(local_endpoint);
        auto [psa, len] = to_sockaddr(sa);
        throw_last_error(::bind(handle, psa, len), "bind");
    }

    auto set_sockopt(socket_native_handle_type handle, int level, int option_name, std::span<const std::byte> value) -> void {
        check_fd(handle);
        throw_last_error(::setsockopt(handle, level, option_name, value.data(), value.size()), "basic_socket::set_option");
    }

    auto get_sockopt(socket_native_handle_type handle, int level, int option_name, std::span<std::byte> value) -> void {
        check_fd(handle);
         ::socklen_t n = value.size();
        throw_last_error(::getsockopt(handle, level, option_name, value.data(), &n), "basic_socket::get_option");
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
        const int fd = ::socket(family, type, protocol_id);
        throw_last_error(fd, "open");
        return fd;
    }

    auto close(socket_native_handle_type handle) -> void {
        if (handle == invalid_socket_handle) return;
        throw_last_error(::close(handle), "close");
    }

    auto max_backlog() noexcept -> std::size_t {
        return SOMAXCONN;
    }

    auto listen(socket_native_handle_type handle, std::size_t backlog) -> void {
        check_fd(handle);
        detail::throw_last_error(::listen(handle, int(backlog)));
    }

    auto connect(socket_native_handle_type handle, const endpoint& peer) -> void {
        check_fd(handle);
        auto sa = endpoint_to_sockaddr_in(peer);
        auto [psa, len] = to_sockaddr(sa);
        throw_last_error(::connect(handle, psa, len), "connect");
    }

    auto accept(socket_native_handle_type handle) -> socket_native_handle_type {
        check_fd(handle);
        auto accepted = ::accept4(handle, nullptr, nullptr, 0);
        detail::throw_last_error(accepted, "accept");
        return accepted;
    }

    auto receive(socket_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t {
        check_fd(handle);
        ::ssize_t n = ::recv(handle, buffer.data(), buffer.size(), 0);
        throw_last_error(n, "receive");
        return n;
    }

    auto send(socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
        check_fd(handle);
        ::ssize_t n = ::send(handle, buffer.data(), buffer.size(), MSG_NOSIGNAL);
        throw_last_error(n, "send");
        return n;
    }

    auto receive_from(socket_native_handle_type handle, std::span<std::byte> buffer, const endpoint& src) -> std::size_t {
        check_fd(handle);
        auto sa = endpoint_to_sockaddr_in(src);
        auto [psa, len] = to_sockaddr(sa);
        ::ssize_t n = ::recvfrom(handle, buffer.data(), buffer.size(), 0, psa, &len);
        throw_last_error(n, "receive_from");
        return n;
    }

    auto send_to(socket_native_handle_type handle, std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t {
        check_fd(handle);
        auto sa = endpoint_to_sockaddr_in(dest);
        auto [psa, len] = to_sockaddr(sa);
        ::ssize_t n = ::sendto(handle, buffer.data(), buffer.size(), MSG_NOSIGNAL, psa, len);
        throw_last_error(n, "send_to");
        return n;
    }
}
