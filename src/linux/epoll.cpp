#include <coio/config.h>
#if not COIO_HAS_EPOLL
#error "we need epoll support to compile this file"
#endif
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <sys/epoll.h>
#include <coio/detail/platform/linux.h>
#include <coio/net/async_operation.h>
#include <coio/net/socket.h>
#include <coio/utils/macros.h>

namespace coio::detail {
    namespace {
        struct epoll_data {
            async_io_operation* input_op = nullptr;
            async_io_operation* output_op = nullptr;
        };

        class epoll_data_registry {
        public:
            auto add(int socket) -> std::pair<epoll_data&, bool> {
                std::unique_lock _{mutex_};
                auto [it, ok] = data_.try_emplace(socket);
                return {it->second, ok};
            }

            auto query(int socket) noexcept -> epoll_data* {
                std::shared_lock _{mutex_};
                auto it = data_.find(socket);
                if (it == data_.end()) return nullptr;
                return &it->second;
            }

            auto remove(int socket) -> bool {
                std::unique_lock _{mutex_};
                return data_.erase(socket);
            }

        private:
            std::shared_mutex mutex_;
            std::unordered_map<int, epoll_data> data_;
        };
    }

}

namespace coio {
    struct io_context::impl {
        impl() noexcept : // NOLINT
            epoll_fd(::epoll_create1(0)) {
            detail::throw_last_error(epoll_fd);
        }

        impl(const impl&) = delete;

        ~impl() {
            ::close(epoll_fd);
        }

        auto operator=(const impl&) -> impl& = delete;

        static impl& of(io_context& context) noexcept {
            return *context.pimpl_;
        }

        auto pull_events(::epoll_event* epoll_events_buffer) -> void {
            auto ready_count = ::epoll_wait(epoll_fd, epoll_events_buffer, epoll_max_wait_count, 0);
            detail::throw_last_error(ready_count);
            for (int i = 0; i < ready_count; ++i) {
                auto op_state = static_cast<detail::epoll_data*>(epoll_events_buffer[i].data.ptr);
                auto evt = epoll_events_buffer[i].events;
                if (evt & EPOLLIN) {
                    COIO_ASSERT(op_state->input_op != nullptr);
                    std::exchange(op_state->input_op, nullptr)->post();
                }
                if (evt & EPOLLOUT) {
                    COIO_ASSERT(op_state->output_op != nullptr);
                    std::exchange(op_state->output_op, nullptr)->post();
                }
            }
        }

        auto register_op(detail::async_io_operation* op) -> void {
            COIO_ASSERT(
                op->category_ == detail::async_io_operation::category::input or
                op->category_ == detail::async_io_operation::category::output
            );

            std::uint32_t event_mask = EPOLLONESHOT; // oneshot
            if (not op->lazy_) event_mask |= EPOLLET;

            int epoll_ctl_op = EPOLL_CTL_ADD;
            auto [data, ok] = registry.add(op->native_handle_);
            if (not ok) { // already registered
                epoll_ctl_op = EPOLL_CTL_MOD;
            }
            if (op->category_ == detail::async_io_operation::category::input) {
                COIO_ASSERT(data.input_op == nullptr);
                event_mask |= EPOLLIN;
                data.input_op = op;
            }
            else {
                COIO_ASSERT(data.output_op == nullptr);
                event_mask |= EPOLLOUT;
                data.output_op = op;
            }

            ::epoll_event event{.events = event_mask, .data = {.ptr = &data}};
            detail::no_errno_here(
                ::epoll_ctl(epoll_fd, epoll_ctl_op, op->native_handle_, &event),
                "coio::io_context::impl::register_op:" COIO_STRINGTIFY(__LINE__)
            );
        }

        auto cancel_ops(detail::socket_native_handle_type handle) -> void {
            if (not registry.remove(handle)) return; // not registered
            detail::no_errno_here(
                ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, handle, nullptr),
                "coio::io_context::impl::cancel_ops:" COIO_STRINGTIFY(__LINE__)
            );
        }

        static constexpr int epoll_max_wait_count = 64;
        int epoll_fd;
        detail::epoll_data_registry registry;
    };

    auto detail::socket_base::cancel() -> void {
        auto& impl = io_context::impl::of(*context_);
        impl.cancel_ops(native_handle());
    }

    io_context::io_context() : pimpl_(std::make_unique<impl>()){}

    io_context::~io_context() = default;

    auto io_context::run() -> void {
        ::epoll_event epoll_events_buffer[impl::epoll_max_wait_count];
        while (not stop_requested()) {
            make_timeout_timers_ready();
            pimpl_->pull_events(epoll_events_buffer);
            poll();
            if (work_count() == 0) break;
        }
    }

    namespace detail {
        auto async_io_operation::await_suspend(std::coroutine_handle<> this_coro) -> void {
            coro_ = this_coro;
            io_context::impl::of(static_cast<io_context&>(context_)).register_op(this);
        }
    }

    auto async_receive_operation::await_ready() noexcept -> bool {
        ::ssize_t n = ::recv(native_handle_, buffer_.data(), buffer_.size(), MSG_DONTWAIT);
        if (n == 0 and zero_as_eof_) exception_ = std::make_exception_ptr(detail::make_eof_error("async_receive"));
        else if (n == -1) {
            exception_ = coio::detail::make_system_error_from_nonblock_errno("async_receive");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_receive_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        ::ssize_t n = ::recv(native_handle_, buffer_.data(), buffer_.size(), 0);
        if (n == 0 and zero_as_eof_) throw detail::make_eof_error("async_receive");
        if (n == -1) {
            COIO_ASSERT(not detail::is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_receive");
        }
        transferred_ = n;
        return transferred_;
    }


    auto async_send_operation::await_ready() noexcept -> bool {
        ::ssize_t n = ::send(native_handle_, buffer_.data(), buffer_.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n == -1) {
            exception_ = coio::detail::make_system_error_from_nonblock_errno("async_send");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_send_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        ::ssize_t n = ::send(native_handle_, buffer_.data(), buffer_.size(), MSG_NOSIGNAL);
        if (n == -1) {
            COIO_ASSERT(not detail::is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_send");
        }
        transferred_ = n;
        return transferred_;
    }


    auto async_receive_from_operation::await_ready() noexcept -> bool {
        auto sa = detail::endpoint_to_sockaddr_in(src_);
        auto [psa, len] = detail::to_sockaddr(sa);
        ::ssize_t n = ::recvfrom(native_handle_, buffer_.data(), buffer_.size(), MSG_DONTWAIT, psa, &len);
        if (n == 0 and zero_as_eof_) exception_ = std::make_exception_ptr(detail::make_eof_error("async_receive_from"));
        else if (n == -1) {
            exception_ = coio::detail::make_system_error_from_nonblock_errno("async_receive_from");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_receive_from_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        auto sa = detail::endpoint_to_sockaddr_in(src_);
        auto [psa, len] = detail::to_sockaddr(sa);
        ::ssize_t n = ::recvfrom(native_handle_, buffer_.data(), buffer_.size(), 0, psa, &len);
        if (n == 0 and zero_as_eof_) throw detail::make_eof_error("async_receive_from");
        if (n == -1) {
            COIO_ASSERT(not detail::is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_receive_from");
        }
        transferred_ = n;
        return transferred_;
    }


    auto async_send_to_operation::await_ready() noexcept -> bool {
        auto sa = detail::endpoint_to_sockaddr_in(dest_);
        auto [psa, len] = detail::to_sockaddr(sa);
        ::ssize_t n = ::sendto(native_handle_, buffer_.data(), buffer_.size(), MSG_DONTWAIT | MSG_NOSIGNAL, psa, len);
        if (n == -1) {
            exception_ = coio::detail::make_system_error_from_nonblock_errno("async_send_to");
            if (not exception_) return false;
        }
        transferred_ = n;
        return true;
    }

    auto async_send_to_operation::await_resume() -> std::size_t {
        if (exception_) std::rethrow_exception(exception_);
        if (transferred_ > 0) return transferred_;
        auto sa = detail::endpoint_to_sockaddr_in(dest_);
        auto [psa, len] = detail::to_sockaddr(sa);
        ::ssize_t n = ::sendto(native_handle_, buffer_.data(), buffer_.size(), MSG_NOSIGNAL, psa, len);
        if (n == -1) {
            COIO_ASSERT(not detail::is_blocking_errno(errno));
            throw std::system_error(errno, std::system_category(), "async_send_to");
        }
        transferred_ = n;
        return transferred_;
    }

    auto async_accept_operation::on_resume_() -> detail::socket_native_handle_type {
        if (exception_) std::rethrow_exception(exception_);
        if (accepted_ == -1) {
            accepted_ = ::accept4(native_handle_, nullptr, nullptr, 0);
            if (accepted_ == -1) {
                COIO_ASSERT(not detail::is_blocking_errno(errno));
                throw std::system_error(errno, std::system_category(), "async_accept");
            }
        }
        return accepted_;
    }

    auto async_connect_operation::await_ready() noexcept -> bool {
        return false;
    }

    auto async_connect_operation::await_resume() -> void {
        auto sa = detail::endpoint_to_sockaddr_in(dest_);
        auto [psa, len] = detail::to_sockaddr(sa);
        detail::throw_last_error(::connect(native_handle_, psa, len), "async_connect");
    }
}