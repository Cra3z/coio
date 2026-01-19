#include <coio/detail/config.h>
#if COIO_HAS_EPOLL
#include <sys/epoll.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <coio/asyncio/epoll_context.h>
#include "../common.h"

namespace coio {
    namespace {
        constexpr int epoll_max_wait_count = 128;
    }

    detail::reactor_interrupter::reactor_interrupter() {
        int pipedes[2];
        detail::throw_last_error(::pipe2(pipedes, O_CLOEXEC | O_NONBLOCK));
        reader_ = pipedes[0];
        writer_ = pipedes[1];
    }

    detail::reactor_interrupter::~reactor_interrupter() {
        no_errno_here(::close(reader_));
        no_errno_here(::close(writer_));
    }

    auto detail::reactor_interrupter::interrupt() -> void {
        std::byte byte{};
        void(::write(writer_, &byte, 1));
    }

    auto detail::reactor_interrupter::reset() -> bool {
        std::byte buffer[1024];
        while (true) {
            ssize_t bytes_read = ::read(reader_, buffer, sizeof(buffer));
            if (bytes_read == sizeof(buffer)) continue;
            if (bytes_read > 0) return true;
            if (bytes_read == 0) return false;
            if (errno == EINTR) continue;
            if (is_blocking_errno(errno)) return true;
            return false;
        }
    }

    auto epoll_context::epoll_op_base::register_event(int event_type, uint32_t extra_flags) noexcept -> bool {
        const bool in_op_registered = data->in_op;
        const bool out_op_registered = data->out_op;
        if (event_type == EPOLLIN /* or event_type == EPOLLPRI */) {
            COIO_ASSERT(not in_op_registered && "an asynchronous input operation shall be initiated after another input operation has completed.");
            data->in_op.store(this);
        }
        else if (event_type == EPOLLOUT) {
            COIO_ASSERT(not out_op_registered && "an asynchronous output operation shall be initiated after another output operation has completed.");
            data->out_op.store(this);
        }
        else unreachable();

        std::uint32_t ev = event_type | extra_flags;
        int epoll_ctl_op = data->registered.exchange(true) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        if (in_op_registered) {
            ev |= EPOLLIN;
            epoll_ctl_op = EPOLL_CTL_MOD;
        }
        if (out_op_registered) {
            ev |= EPOLLOUT;
            epoll_ctl_op = EPOLL_CTL_MOD;
        }
        ::epoll_event event {
            .events = ev,
            .data = {.ptr = data}
        };
        return ::epoll_ctl(context_.epoll_fd_, epoll_ctl_op, fd, &event) == 0;
    }

    epoll_context::scheduler::io_object::io_object(epoll_context& ctx, int fd) :
        ctx_(ctx),
        fd_(fd),
        data_(ctx.new_epoll_data()) {
        if (fd != -1 and (S_ISREG(fd) or S_ISDIR(fd))) [[unlikely]] {
            throw std::system_error{
                std::make_error_code(std::errc::operation_not_permitted),
                "the target file `fd` doesn't support epoll"
            };
        }
    }

    epoll_context::scheduler::io_object::~io_object() {
        cancel();
        ctx_.get().reclaim_epoll_data(data_);
    }

    auto epoll_context::scheduler::io_object::release() -> int {
        if (fd_ == -1) return -1;
        COIO_ASSERT(data_ != nullptr);
        cancel();
        if (data_->registered.load()) {
            detail::throw_last_error(::epoll_ctl(ctx_.get().epoll_fd_, EPOLL_CTL_DEL, fd_, nullptr));
        }
        ctx_.get().reclaim_epoll_data(std::exchange(data_, nullptr));
        return std::exchange(fd_, -1);
    }

    auto epoll_context::scheduler::io_object::cancel() -> void {
        if (fd_ == -1) return;
        COIO_ASSERT(data_ != nullptr);
        for (auto op : {data_->in_op.exchange(nullptr), data_->out_op.exchange(nullptr)}) {
            if (op == nullptr) continue;
            if (op->unhandled_stopped_ == nullptr) std::terminate();
            op->unhandled_stopped_(op->coro_).resume();
        }
    }

    epoll_context::epoll_context(std::pmr::memory_resource& memory_resource): epoll_context(nullptr, memory_resource) {
        {
            epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
            detail::throw_last_error(epoll_fd_);
        }
        {
            ::epoll_event event {
                .events = std::uint32_t(EPOLLIN | EPOLLET),
                .data = {.ptr = &interrupter_}
            };
            detail::throw_last_error(::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, interrupter_.watcher(), &event));
        }
    }

    epoll_context::~epoll_context() {
        request_stop();
        ::close(epoll_fd_);
    }

    auto epoll_context::do_one(bool infinite) -> bool {
        if (stop_requested()) return false;

        ::epoll_event ready_events[epoll_max_wait_count];
        int timeout = infinite ? -1 : 0;
        do {
            timer_queue_.take_ready_timers(op_queue_);
            if (const auto op = op_queue_.try_dequeue()) {
                op->coro_.resume();
                return true;
            }

            if (infinite) {
                using milliseconds = std::chrono::duration<int, std::milli>;
                if (const auto earliest = timer_queue_.earliest()) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto msec = std::chrono::duration_cast<milliseconds>(*earliest - now).count();
                    timeout = std::max(msec, 0);
                    if (timeout > 0) timeout += 1;
                }
            }
            const int ready_count = ::epoll_wait(epoll_fd_, ready_events, epoll_max_wait_count, timeout);
            if (ready_count == -1 and errno == EINTR) continue;
            detail::throw_last_error(ready_count, "epoll_wait");

            op_queue local_op_queue;
            for (int i = 0; i < ready_count; ++i) {
                const auto& [event, data] = ready_events[i];
                COIO_ASSERT(data.ptr != nullptr);
                if (data.ptr == &interrupter_) {
                    interrupter_.reset();
                    continue;
                }

                const auto fd_data = static_cast<epoll_data*>(data.ptr);
                epoll_op_base* op = nullptr;

                // if (event & EPOLLPRI) {} TODO: handle EPOLLPRI for out-of-band data
                if (event & EPOLLIN) {
                    op = fd_data->in_op.exchange(nullptr);
                }
                if (event & EPOLLOUT) {
                    op = fd_data->out_op.exchange(nullptr);
                }

                if (op != nullptr) {
                    op->next_ = nullptr;
                    op->perform(op);
                    local_op_queue.unsynchronized_enqueue(*op);
                }
            }

            op_queue_.splice(std::move(local_op_queue));
        }
        while (infinite and not stop_requested());
        return false;
    }

    auto epoll_context::new_epoll_data() -> epoll_data* {
        return allocator_.new_object<epoll_data>();
    }

    auto epoll_context::reclaim_epoll_data(epoll_data* data) noexcept -> void {
        if (data == nullptr) return;
        return allocator_.delete_object(data);
    }

    auto epoll_context::cancel_op(int event, epoll_op_base* op) -> void {
        COIO_ASSERT(op != nullptr);
        const auto data = op->data;
        [[maybe_unused]] const auto registered_op = event == EPOLLIN ? data->in_op.exchange(nullptr) : data->out_op.exchange(nullptr);
        COIO_ASSERT(registered_op == op);
        if (op->unhandled_stopped_ == nullptr) std::terminate();
        op->unhandled_stopped_(op->coro_).resume();
    }

    namespace detail {
        template<>
        auto epoll_op_base_for<async_read_some_t>::start() noexcept -> bool {
            if (not register_event(EPOLLIN, 0)) {
                result.error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_op_base_for<async_read_some_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            const ::ssize_t n = ::read(fd, buffer.data(), buffer.size());
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
        }

        template<>
        auto epoll_op_base_for<async_read_some_t>::cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
            [[maybe_unused]] auto op = data->in_op.exchange(nullptr);
            COIO_ASSERT(op == this);
            coro_.resume();
        }


        template<>
        auto epoll_op_base_for<async_write_some_t>::start() noexcept -> bool {
            if (not register_event(EPOLLOUT, 0)) {
                result.error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_op_base_for<async_write_some_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            const ::ssize_t n = ::write(fd, buffer.data(), buffer.size());
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
        }

        template<>
        auto epoll_op_base_for<async_write_some_t>::cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        template<>
        auto epoll_op_base_for<async_receive_t>::start() noexcept -> bool {
            const ::ssize_t n = ::recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLIN, EPOLLET)) {
                        result.error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
            immediate_complete();
            return true;
        }

        template<>
        auto epoll_op_base_for<async_receive_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            const ::ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
        }

        template<>
        auto epoll_op_base_for<async_receive_t>::cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        template<>
        auto epoll_op_base_for<async_send_t>::start() noexcept -> bool {
            const ::ssize_t n = ::send(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLOUT, EPOLLET)) {
                        result.error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
            immediate_complete();
            return true;
        }

        template<>
        auto epoll_op_base_for<async_send_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            const ::ssize_t n = ::send(fd, buffer.data(), buffer.size(), MSG_NOSIGNAL);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
        }

        template<>
        auto epoll_op_base_for<async_send_t>::cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        template<>
        auto epoll_op_base_for<async_receive_from_t>::start() noexcept -> bool {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            const ::ssize_t n = ::recvfrom(fd, buffer.data(), buffer.size(), MSG_DONTWAIT, psa, &len);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLIN, EPOLLET)) {
                        result.error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
            immediate_complete();
            return true;
        }

        template<>
        auto epoll_op_base_for<async_receive_from_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            const ::ssize_t n = ::recvfrom(fd, buffer.data(), buffer.size(), MSG_DONTWAIT, psa, &len);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
        }

        template<>
        auto epoll_op_base_for<async_receive_from_t>::cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        template<>
        auto epoll_op_base_for<async_send_to_t>::start() noexcept -> bool {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::sendto(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL, psa, len);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLOUT, EPOLLET)) {
                        result.error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
            immediate_complete();
            return true;
        }

        template<>
        auto epoll_op_base_for<async_send_to_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::sendto(fd, buffer.data(), buffer.size(), MSG_NOSIGNAL, psa, len);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code(errno, std::system_category()));
            }
            else {
                result.value(n);
            }
        }

        template<>
        auto epoll_op_base_for<async_send_to_t>::cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        template<>
        auto epoll_op_base_for<async_accept_t>::start() noexcept -> bool {
            if (not register_event(EPOLLIN, 0)) {
                result.error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_op_base_for<async_accept_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            auto accepted_ = ::accept4(fd, nullptr, nullptr, 0);
            if (accepted_ == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code{errno, std::system_category()});
            }
            else {
                result.value(accepted_);
            }
        }

        template<>
        auto epoll_op_base_for<async_accept_t>::cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }

        template<>
        auto epoll_op_base_for<async_connect_t>::start() noexcept -> bool {
            if (not register_event(EPOLLOUT, 0)) {
                result.error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_op_base_for<async_connect_t>::do_perform() noexcept -> void {
            COIO_ASSERT(not result.ready());
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = detail::to_sockaddr(sa);
            if (::connect(fd, psa, len) == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.error(std::error_code{errno, std::system_category()});
            }
            else {
                result.value();
            }
        }

        template<>
        auto epoll_op_base_for<async_connect_t>::cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }
    }
}
#endif