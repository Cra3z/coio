#include <coio/config.h>
#include <cstdio>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <coio/detail/platform/linux.h>
#include <coio/utils/utility.h>
#include <coio/net/socket.h>

namespace coio {
    namespace detail {
        auto receive(coio::detail::socket_native_handle_type handle, std::span<std::byte> buffer, bool zero_as_eof) -> std::size_t {
            ::ssize_t n = ::recv(handle, buffer.data(), buffer.size(), 0);
            if (n == 0 and zero_as_eof) throw make_eof_error("receive");
            throw_last_error(n, "receive");
            return n;
        }

        auto send(coio::detail::socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
            ::ssize_t n = ::send(handle, buffer.data(), buffer.size(), 0);
            throw_last_error(n, "send");
            return n;
        }

        auto receive_from(
            coio::detail::socket_native_handle_type handle,
            std::span<std::byte> buffer,
            const endpoint& src,
            bool zero_as_eof
        ) -> std::size_t {
            auto sa = endpoint_to_sockaddr_in(src);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::recvfrom(handle, buffer.data(), buffer.size(), 0, psa, &len);
            if (n == 0 and zero_as_eof) throw make_eof_error("receive_from");
            throw_last_error(n, "receive_from");
            return n;
        }

        auto send_to(
            coio::detail::socket_native_handle_type handle,
            std::span<const std::byte> buffer,
            const endpoint& dest
        ) -> std::size_t {
            auto sa = endpoint_to_sockaddr_in(dest);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::sendto(handle, buffer.data(), buffer.size(), 0, psa, len);
            throw_last_error(n, "send_to");
            return n;
        }

        auto socket_base::local_endpoint() const noexcept -> endpoint {
            COIO_ASSERT(this->is_open());
            ::sockaddr_storage addr{};
            ::socklen_t addr_len = sizeof(addr);
            [[maybe_unused]] auto ret = ::getsockname(handle_, reinterpret_cast<::sockaddr*>(&addr), &addr_len);
            COIO_ASSERT(ret != -1);
            return sockaddr_storage_to_endpoint(addr);
        }

        auto socket_base::remote_endpoint() const noexcept -> endpoint {
            COIO_ASSERT(this->is_open());
            ::sockaddr_storage addr{};
            ::socklen_t addr_len = sizeof(addr);
            [[maybe_unused]] auto ret = ::getpeername(handle_, reinterpret_cast<::sockaddr*>(&addr), &addr_len);
            COIO_ASSERT(ret != -1);
            return sockaddr_storage_to_endpoint(addr);
        }

        auto socket_base::open_(int family, int type, int protocol_id) -> void {
            if (is_open()) [[unlikely]] throw std::system_error{error::already_open, "this socket is already open."};
            auto fd = ::socket(family, type, protocol_id);
            throw_last_error(fd, "open");
            reset_(fd);
        }

        auto socket_base::reset_(native_handle_type new_handle) noexcept -> void {
            close();
            handle_ = new_handle;
        }

        auto socket_base::close() noexcept -> void {
            cancel();
            ::close(std::exchange(handle_, invalid_socket_handle_value));
        }

        auto socket_base::shutdown(shutdown_type how) -> void {
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
            throw_last_error(::shutdown(handle_, h));
        }

        auto socket_base::reuse_address() -> void {
            int value = 1;
            throw_last_error(::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int)), "reuse_address");
        }

        auto socket_base::set_non_blocking(bool mode) -> void {
            COIO_ASSERT(this->is_open());
            int old_flag = ::fcntl(handle_, F_GETFL);
            throw_last_error(old_flag, "set_non_blocking");
            int new_flag = mode ? (old_flag | O_NONBLOCK) : (old_flag & ~O_NONBLOCK);
            throw_last_error(::fcntl(handle_, F_SETFL, new_flag), "set_non_blocking");
        }

        auto socket_base::is_non_blocking() const noexcept -> bool {
            int flag = ::fcntl(handle_, F_GETFL);
            detail::no_errno_here(flag, "unexcepted error in `coio::socket_base::is_non_blocking`");
            return flag & O_NONBLOCK;
        }

        auto socket_base::bind(const endpoint& addr) -> void {
            COIO_ASSERT(this->is_open());
            auto sa = endpoint_to_sockaddr_in(addr);
            auto [psa, len] = to_sockaddr(sa);
            throw_last_error(::bind(handle_, psa, len), "bind");
        }

        auto socket_base::connect_(const endpoint& addr) -> void {
            COIO_ASSERT(this->is_open());
            auto sa = endpoint_to_sockaddr_in(addr);
            auto [psa, len] = to_sockaddr(sa);
            throw_last_error(::connect(handle_, psa, len), "connect");
        }

    }

    tcp_acceptor::tcp_acceptor(io_context& context, const endpoint& addr, std::size_t backlog, bool reuse_addr) : tcp_acceptor(context, ::socket(addr.ip().is_v4() ? AF_INET : AF_INET6, SOCK_STREAM, IPPROTO_TCP)) {
        if (reuse_addr) reuse_address();
        bind(addr);
        listen(backlog);
    }

    auto tcp_acceptor::listen(std::size_t backlog) -> void {
        detail::throw_last_error(::listen(handle_, int(backlog)));
    }

    auto tcp_acceptor::accept(tcp_socket& out) -> void {
        auto accepted = ::accept4(handle_, nullptr, nullptr, 0);
        detail::throw_last_error(accepted, "accept");
        out.reset_(accepted);
    }

    auto tcp_acceptor::max_backlog() noexcept -> std::size_t {
        return SOMAXCONN;
    }

    auto tcp_socket::connect(const endpoint& addr) -> void {
        if (not is_open()) open(addr.ip().is_v4() ? tcp::v4() : tcp::v6());
        connect_(addr);
    }

    auto tcp_socket::async_connect(const endpoint& addr) -> async_connect_t {
        if (not is_open()) open(addr.ip().is_v4() ? tcp::v4() : tcp::v6());
        return async_connect_(addr);
    }

    auto udp_socket::connect(const endpoint& addr) -> void {
        if (not is_open()) open(addr.ip().is_v4() ? udp::v4() : udp::v6());
        connect_(addr);
    }

    auto udp_socket::async_connect(const endpoint& addr) -> async_connect_t {
        if (not is_open()) open(addr.ip().is_v4() ? udp::v4() : udp::v6());
        return async_connect_(addr);
    }
}
