#include <coio/config.h>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#if COIO_HAS_EPOLL
#include <sys/epoll.h>
#elif COIO_HAS_IO_URING
#include <linux/io_uring.h>
#endif
#include <coio/core.h>
#include <coio/error_code.h>
#include <coio/utils/utility.h>
#include <coio/utils/scope_exit.h>
#include <coio/utils/macros.h>
#include <coio/net/socket.h>

namespace coio {

#if COIO_HAS_EPOLL

    namespace {

        auto throw_last_error(::ssize_t value, const char* msg = nullptr) ->void {
            // assume: msg is null or null-terminated.
            if (value != -1) return;
            if (msg == nullptr) throw std::system_error(errno, std::system_category());
            throw std::system_error(errno, std::system_category(), msg);
        }

        auto no_errno_here(::ssize_t value, const char* msg = nullptr) ->void {
            // assume: msg is null or null-terminated.
            if (value != -1) return;
            std::perror(msg);
            std::terminate();
        }

        auto is_blocking_errno(int errno_) noexcept ->bool {
#if EWOULDBLOCK == EAGAIN
            return errno_ == EWOULDBLOCK;
#else
            return errno_ == EWOULDBLOCK or errno_ == EAGAIN;
#endif
        }

        [[nodiscard]]
        auto make_system_error_from_nonblock_errno(const char* msg = nullptr) noexcept -> std::exception_ptr {
            int errno_ = errno;
            if (is_blocking_errno(errno_)) return {};
            return std::make_exception_ptr(
                msg ?
                std::system_error(errno_, std::system_category(), msg) :
                std::system_error(errno_, std::system_category())
            );
        }

        auto endpoint_to_sockaddr_in(const net::endpoint& addr) noexcept -> std::variant<::sockaddr_in, ::sockaddr_in6> {
            if (addr.ip().is_v4()) {
                return ::sockaddr_in{
                    .sin_family = AF_INET,
                    .sin_port = ::htons(addr.port()),
                    .sin_addr = std::bit_cast<::in_addr>(addr.ip().v4())
                };
            }
            return ::sockaddr_in6{
                .sin6_family = AF_INET6,
                .sin6_port = ::htons(addr.port()),
                .sin6_addr = std::bit_cast<::in6_addr>(addr.ip().v6())
            };
        }

        auto sockaddr_to_endpoint(::sockaddr* sa) noexcept -> net::endpoint {
            switch (sa->sa_family) {
            case AF_INET: {
                auto ipv4 = reinterpret_cast<::sockaddr_in*>(sa);
                return net::endpoint{std::bit_cast<net::ipv4_address>(ipv4->sin_addr), ::ntohs(ipv4->sin_port)};
            }
            case AF_INET6: {
                auto ipv6 = reinterpret_cast<::sockaddr_in6*>(sa);
                return net::endpoint{std::bit_cast<net::ipv6_address>(ipv6->sin6_addr), ::ntohs(ipv6->sin6_port)};
            }
            default: unreachable();
            }
        }

        auto sockaddr_storage_to_endpoint(::sockaddr_storage& addr) noexcept -> net::endpoint {
            switch (addr.ss_family) {
            case AF_INET: {
                ::sockaddr_in ipv4{};
                std::memcpy(&ipv4, &addr, sizeof(ipv4));
                return {std::bit_cast<net::ipv4_address>(ipv4.sin_addr), ::ntohs(ipv4.sin_port)};
            }
            case AF_INET6: {
                ::sockaddr_in6 ipv6{};
                std::memcpy(&ipv6, &addr, sizeof(ipv6));
                return {std::bit_cast<net::ipv6_address>(ipv6.sin6_addr), ::ntohs(ipv6.sin6_port)};
            }
            default: unreachable();
            }
        }

        auto to_sockaddr(std::variant<::sockaddr_in, ::sockaddr_in6>& sa)-> std::pair<::sockaddr*, ::socklen_t> {
            return std::visit([](auto& sai) noexcept -> std::pair<::sockaddr*, ::socklen_t> {
                return {reinterpret_cast<::sockaddr*>(&sai), sizeof(sai)};
            }, sa);
        }

        auto make_eof_error(const char* msg = nullptr) -> std::system_error {
            return msg ? std::system_error{error::eof, msg} : std::system_error{error::eof};
        }

    }

    auto error::gai_category_t::message(int ec) const -> std::string {
        return ::gai_strerror(ec);
    }

    struct io_context::impl {
        static auto of(io_context& context) ->impl& {
            return *context.pimpl_;
        }

        impl() noexcept : epoll_fd(::epoll_create1(0)) {
            throw_last_error(epoll_fd);
        }

        impl(const impl&) = delete;

        ~impl() {
            ::close(epoll_fd);
        }

        auto operator= (const impl&) ->impl& = delete;

        auto pull(::epoll_event* epoll_events_buffer) -> void {
            auto ready_count = ::epoll_wait(epoll_fd, epoll_events_buffer, epoll_max_wait_count, 0);
            throw_last_error(ready_count);
            for (int i = 0; i < ready_count; ++i) {
                auto op_state = static_cast<net::detail::op_state*>(epoll_events_buffer[i].data.ptr);
                auto evt = epoll_events_buffer[i].events;
                bool unregister_event = false;
                if (evt & EPOLLIN) {
                    COIO_DCHECK(op_state->in_op != nullptr);
                    std::exchange(op_state->in_op, nullptr)->post();
                    unregister_event = op_state->out_op == nullptr;
                }
                if (evt & EPOLLOUT) {
                    COIO_DCHECK(op_state->out_op != nullptr);
                    std::exchange(op_state->out_op, nullptr)->post();
                    unregister_event = op_state->in_op == nullptr;
                }
                if (unregister_event) {
                    no_errno_here(
                        ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, op_state->handle, &epoll_events_buffer[i]),
                        "coio::io_context::impl::push:" COIO_STRINGTIFY(__LINE__)
                    );
                };
            }
        }

        auto push(net::detail::op_state* op_state_, net::detail::async_in_operation_base* op, std::uint32_t extra_flags = EPOLLET) ->void {
            COIO_ASSERT(op_state_->in_op == nullptr);
            op_state_->in_op = op;
            std::uint32_t events = EPOLLIN | EPOLLONESHOT | extra_flags;
            int epoll_ctl_op = EPOLL_CTL_ADD;
            if (op_state_->out_op) {
                events |= EPOLLOUT;
                epoll_ctl_op = EPOLL_CTL_MOD;
            }
            ::epoll_event event{.events = events, .data = {.ptr = op_state_}};
            no_errno_here(
                ::epoll_ctl(epoll_fd, epoll_ctl_op, op_state_->handle, &event),
                "coio::io_context::impl::push:" COIO_STRINGTIFY(__LINE__)
            );
        }

        auto push(net::detail::op_state* op_state_, net::detail::async_out_operation_base* op, std::uint32_t extra_flags = EPOLLET) ->void {
            COIO_ASSERT(op_state_->out_op == nullptr);
            op_state_->out_op = op;
            std::uint32_t events = EPOLLOUT | EPOLLONESHOT | extra_flags;
            int epoll_ctl_op = EPOLL_CTL_ADD;
            if (op_state_->in_op) {
                events |= EPOLLIN;
                epoll_ctl_op = EPOLL_CTL_MOD;
            }
            ::epoll_event event{.events = events, .data = {.ptr = op_state_}};
            no_errno_here(
                ::epoll_ctl(epoll_fd, epoll_ctl_op, op_state_->handle, &event),
                "coio::io_context::impl::push:" COIO_STRINGTIFY(__LINE__)
            );
        }

        int epoll_fd;
        static constexpr int epoll_max_wait_count = 32;
    };
#endif

    io_context::io_context() : pimpl_(std::make_unique<impl>()) {}

    io_context::~io_context() = default;

    auto io_context::run() -> void {
        ::epoll_event epoll_events_buffer[impl::epoll_max_wait_count];
        while (not stop_requested()) {
            make_timeout_timers_ready();
            pimpl_->pull(epoll_events_buffer);
            poll();
            if (work_count() == 0) break;
        }
    }

}

namespace coio::net {

    namespace detail {
        auto sync_recv(socket_native_handle_type handle, std::span<std::byte> buffer, bool zero_as_eof) -> std::size_t {
            ::ssize_t n = ::recv(handle, buffer.data(), buffer.size(), 0);
            if (n == 0 and zero_as_eof) throw make_eof_error("receive");
            throw_last_error(n, "receive");
            return n;
        }

        auto sync_send(socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
            ::ssize_t n = ::send(handle, buffer.data(), buffer.size(), 0);
            throw_last_error(n, "send");
            return n;
        }

        auto sync_recv_from(
            socket_native_handle_type handle,
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

        auto sync_send_to(
            socket_native_handle_type handle,
            std::span<const std::byte> buffer,
            const endpoint& dest
        ) -> std::size_t {
            auto sa = endpoint_to_sockaddr_in(dest);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::sendto(handle, buffer.data(), buffer.size(), 0, psa, len);
            throw_last_error(n, "send_to");
            return n;
        }

        auto async_in_operation_base::await_suspend(std::coroutine_handle<> this_coro) -> void {
            coro_ = this_coro;
            io_context::impl::of(context()).push(op_state_, this);
        }

        auto async_out_operation_base::await_suspend(std::coroutine_handle<> this_coro) -> void {
            coro_ = this_coro;
            io_context::impl::of(context()).push(op_state_, this);
        }

    }

    auto tcp::v4() noexcept -> tcp {
        return tcp{AF_INET};
    }

    auto tcp::v6() noexcept -> tcp {
        return tcp{AF_INET6};
    }

    auto tcp::type() noexcept -> int {
        return SOCK_STREAM;
    }

    auto tcp::protocol_id() noexcept -> int {
        return IPPROTO_TCP;
    }

    auto udp::v4() noexcept -> udp {
        return udp{AF_INET};
    }

    auto udp::v6() noexcept -> udp {
        return udp{AF_INET6};
    }

    auto udp::type() noexcept -> int {
        return SOCK_DGRAM;
    }

    auto udp::protocol_id() noexcept -> int {
        return IPPROTO_UDP;
    }

    auto reverse_bytes(std::span<std::byte> bytes) noexcept -> void {
        std::ranges::reverse(bytes);
    }

    ipv4_address::ipv4_address(std::uint32_t host_u32) noexcept : net_u32_(::htonl(host_u32)) {}

    ipv4_address::ipv4_address(const std::string& str) {
        if (::inet_pton(AF_INET, str.c_str(), &net_u32_) == -1) {
            throw std::invalid_argument{"invalid ipv4 network address in dotted-decimal format."};
        }
    }

    auto ipv4_address::to_string() const -> std::string {
        char buf[INET_ADDRSTRLEN];
        return ::inet_ntop(AF_INET, &net_u32_, buf, INET_ADDRSTRLEN);
    }

    auto ipv4_address::operator==(const ipv4_address& other) const noexcept -> bool {
        return ::ntohl(net_u32_) == ::ntohl(other.net_u32_);
    }

    auto ipv4_address::operator<=>(const ipv4_address& other) noexcept -> std::strong_ordering {
        return ::ntohl(net_u32_) <=> ::ntohl(other.net_u32_);
    }

    ipv6_address::ipv6_address(const std::string& str) {
        if (::inet_pton(AF_INET6, str.c_str(), val_) == -1) {
            throw std::invalid_argument{"invalid format for ipv6 network address."};
        }
    }

    auto ipv6_address::to_string() const -> std::string {
        char buf[INET6_ADDRSTRLEN];
        return ::inet_ntop(AF_INET6, val_, buf, INET6_ADDRSTRLEN);
    }

    auto async_receive_operation::await_ready() noexcept -> bool {
        ::ssize_t n = ::recv(op_state_->handle, buffer_.data(), buffer_.size(), MSG_DONTWAIT);
        if (n == 0 and zero_as_eof_) exception_ = std::make_exception_ptr(make_eof_error("async_receive"));
        else if (n == -1) {
            exception_ = make_system_error_from_nonblock_errno("async_receive");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_receive_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        ::ssize_t n = ::recv(op_state_->handle, buffer_.data(), buffer_.size(), 0);
        if (n == 0 and zero_as_eof_) throw make_eof_error("async_receive");
        if (n == -1) {
            COIO_ASSERT(not is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_receive");
        }
        transferred_ = n;
        return transferred_;
    }


    auto async_send_operation::await_ready() noexcept -> bool {
        ::ssize_t n = ::send(op_state_->handle, buffer_.data(), buffer_.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n == -1) {
            exception_ = make_system_error_from_nonblock_errno("async_send");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_send_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        ::ssize_t n = ::send(op_state_->handle, buffer_.data(), buffer_.size(), MSG_NOSIGNAL);
        if (n == -1) {
            COIO_ASSERT(not is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_send");
        }
        transferred_ = n;
        return transferred_;
    }


    auto async_receive_from_operation::await_ready() noexcept -> bool {
        auto sa = endpoint_to_sockaddr_in(src_);
        auto [psa, len] = to_sockaddr(sa);
        ::ssize_t n = ::recvfrom(op_state_->handle, buffer_.data(), buffer_.size(), MSG_DONTWAIT, psa, &len);
        if (n == 0 and zero_as_eof_) exception_ = std::make_exception_ptr(make_eof_error("async_receive_from"));
        else if (n == -1) {
            exception_ = make_system_error_from_nonblock_errno("async_receive_from");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_receive_from_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        auto sa = endpoint_to_sockaddr_in(src_);
        auto [psa, len] = to_sockaddr(sa);
        ::ssize_t n = ::recvfrom(op_state_->handle, buffer_.data(), buffer_.size(), 0, psa, &len);
        if (n == 0 and zero_as_eof_) throw make_eof_error("async_receive_from");
        if (n == -1) {
            COIO_ASSERT(not is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_receive_from");
        }
        transferred_ = n;
        return transferred_;
    }
    

    auto async_send_to_operation::await_ready() noexcept -> bool {
        auto sa = endpoint_to_sockaddr_in(dest_);
        auto [psa, len] = to_sockaddr(sa);
        ::ssize_t n = ::sendto(op_state_->handle, buffer_.data(), buffer_.size(), MSG_DONTWAIT | MSG_NOSIGNAL, psa, len);
        if (n == -1) {
            exception_ = make_system_error_from_nonblock_errno("async_send_to");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_send_to_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        auto sa = endpoint_to_sockaddr_in(dest_);
        auto [psa, len] = to_sockaddr(sa);
        ::ssize_t n = ::sendto(op_state_->handle, buffer_.data(), buffer_.size(), MSG_NOSIGNAL, psa, len);
        if (n == -1) {
            COIO_ASSERT(not is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_send_to");
        }
        transferred_ = n;
        return transferred_;
    }

    auto async_accept_operation::await_suspend(std::coroutine_handle<> this_coro) -> void {
        coro_ = this_coro;
        io_context::impl::of(context()).push(op_state_, this, 0);
    }

    auto async_accept_operation::output_accepted_(tcp_socket& peer) -> void {
        if (exception_) std::rethrow_exception(exception_);
        if (accepted_ == -1) {
            accepted_ = ::accept4(op_state_->handle, nullptr, nullptr, 0);
            if (accepted_ == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                throw std::system_error(errno, std::system_category(), "async_accept");
            }
        }
        peer.reset_(accepted_);
    }

    auto async_connect_operation::await_ready() noexcept -> bool {
        auto sa = endpoint_to_sockaddr_in(dest_);
        auto [psa, len] = to_sockaddr(sa);
        if (::connect(op_state_->handle, psa, len) == -1) {
            const auto er = errno;
            if (er == EINPROGRESS) return false;
            exception_ = std::make_exception_ptr(std::system_error(er, std::system_category(), "async_connect"));
        }
        else connected_ = true;
        return true;
    }

    auto async_connect_operation::await_resume() -> void {
        if (exception_) std::rethrow_exception(exception_);
        if (connected_) return;
        auto sa = endpoint_to_sockaddr_in(dest_);
        auto [psa, len] = to_sockaddr(sa);
        throw_last_error(::connect(op_state_->handle, psa, len), "async_connect");
    }

    namespace detail {

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
            *this = socket_base(*context_, fd);
        }

        auto socket_base::reset_(native_handle_type new_handle) noexcept -> void {
            ::close(std::exchange(handle_, new_handle));
            if (op_state_) op_state_->reset(new_handle);
            else op_state_ = new op_state{new_handle};
        }

        auto socket_base::close() noexcept -> void {
            ::close(std::exchange(handle_, invalid_socket_handle_value));
            delete std::exchange(op_state_, nullptr);
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

        auto socket_base::set_non_blocking() -> void {
            COIO_ASSERT(this->is_open());
            int old_flag = ::fcntl(handle_, F_GETFL);
            throw_last_error(old_flag, "set_non_blocking");
            throw_last_error(::fcntl(handle_, F_SETFL, old_flag | O_NONBLOCK), "set_non_blocking");
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

        const int resolve_query_t::canonical_name     = AI_CANONNAME;
        const int resolve_query_t::passive            = AI_PASSIVE;
        const int resolve_query_t::numeric_host       = AI_NUMERICHOST;
        const int resolve_query_t::numeric_service    = AI_NUMERICSERV;
        const int resolve_query_t::v4_mapped          = AI_V4MAPPED;
        const int resolve_query_t::all_matching       = AI_ALL;
        const int resolve_query_t::address_configured = AI_ADDRCONFIG;

        auto resolve_impl(resolve_query_t query, int socktype, int protocol_id) -> generator<resolve_result_t> {
            return resolve_impl(std::move(query), AF_UNSPEC, socktype, protocol_id);
        }

        auto resolve_impl(resolve_query_t query, int family, int socktype, int protocol_id) -> generator<resolve_result_t> {
            ::addrinfo hints{
                .ai_flags = query.flags,
                .ai_family = family,
                .ai_socktype = socktype,
                .ai_protocol = protocol_id,
            };
            ::addrinfo* ai_head = nullptr;
            if (int ec = ::getaddrinfo(
                query.host_name.empty() ? nullptr : query.host_name.c_str(),
                query.service_name.empty() ? nullptr : query.service_name.c_str(),
                &hints, &ai_head
            )) throw std::system_error(ec, error::gai_category());
            scope_exit _{[ai_head]() noexcept {
                ::freeaddrinfo(ai_head);
            }};
            for (auto ai_node = ai_head; ai_node != nullptr; ai_node = ai_node->ai_next) {
                co_yield {sockaddr_to_endpoint(ai_node->ai_addr), ai_node->ai_canonname ? ai_node->ai_canonname : ""};
            }
        }
    }

    tcp_acceptor::tcp_acceptor(io_context& context, const endpoint& addr, std::size_t backlog, bool reuse_addr) : tcp_acceptor(context, ::socket(addr.ip().is_v4() ? AF_INET : AF_INET6, SOCK_STREAM, IPPROTO_TCP)) {
        if (reuse_addr) reuse_address();
        bind(addr);
        listen(backlog);
    }

    auto tcp_acceptor::listen(std::size_t backlog) -> void {
        throw_last_error(::listen(handle_, int(backlog)));
    }

    auto tcp_acceptor::accept(tcp_socket& out) -> void {
        auto accepted = ::accept4(handle_, nullptr, nullptr, 0);
        throw_last_error(accepted, "accept");
        out.reset_(accepted);
    }

    auto tcp_acceptor::max_backlog() noexcept -> std::size_t {
        return SOMAXCONN;
    }

    auto tcp_socket::connect(const endpoint& addr) -> void {
        if (not is_open()) open(addr.ip().is_v4() ? tcp::v4() : tcp::v6());
        connect_(addr);
    }

    auto tcp_socket::async_connect(const endpoint& addr) -> async_connect_operation {
        if (not is_open()) open(addr.ip().is_v4() ? tcp::v4() : tcp::v6());
        return async_connect_(addr);
    }

    auto udp_socket::connect(const endpoint& addr) -> void {
        if (not is_open()) open(addr.ip().is_v4() ? udp::v4() : udp::v6());
        connect_(addr);
    }

    auto udp_socket::async_connect(const endpoint& addr) -> async_connect_operation {
        if (not is_open()) open(addr.ip().is_v4() ? udp::v4() : udp::v6());
        return async_connect_(addr);
    }
}
