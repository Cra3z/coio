// ReSharper disable CppMemberFunctionMayBeConst
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

    auto epoll_context::epoll_node::register_event(int event_type, std::uint32_t extra_flags) noexcept -> bool {
        std::scoped_lock _{data->fd_lock};
        const bool in_op_registered = data->in_op;
        const bool out_op_registered = data->out_op;
        if (event_type == EPOLLIN /* or event_type == EPOLLPRI */) {
            COIO_ASSERT(not in_op_registered && "an asynchronous input operation shall be initiated after another input operation has completed.");
            data->in_op = this;
        }
        else if (event_type == EPOLLOUT) {
            COIO_ASSERT(not out_op_registered && "an asynchronous output operation shall be initiated after another output operation has completed.");
            data->out_op = this;
        }
        else unreachable();

        std::uint32_t ev = event_type | extra_flags;
        int epoll_ctl_op = data->events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

        if (in_op_registered) {
            ev |= EPOLLIN;
            epoll_ctl_op = EPOLL_CTL_MOD;
        }
        if (out_op_registered) {
            ev |= EPOLLOUT;
            epoll_ctl_op = EPOLL_CTL_MOD;
        }

        if (ev == data->events) return true;

        ::epoll_event event{.events = ev, .data = {.ptr = data}};
        const bool ok = ::epoll_ctl(context_.epoll_fd_, epoll_ctl_op, fd, &event) == 0;
        if (ok) data->events = ev;
        return ok;
    }

    epoll_context::scheduler::io_object::io_object(epoll_context& ctx, int fd) :
        ctx_(ctx),
        fd_(fd),
        data_(ctx.new_epoll_data()) {
        if (fd == -1) return;
        struct ::stat st{};
        if (::fstat(fd, &st) == 0 and (S_ISREG(st.st_mode) or S_ISDIR(st.st_mode))) [[unlikely]] {
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
        epoll_context& context = ctx_;
        cancel();
        {
            std::scoped_lock _{context.bolt_, data_->fd_lock};
            if (data_->events != 0) {
                detail::throw_last_error(::epoll_ctl(context.epoll_fd_, EPOLL_CTL_DEL, fd_, nullptr));
            }
        }
        ctx_.get().reclaim_epoll_data(std::exchange(data_, nullptr));
        return std::exchange(fd_, -1);
    }

    auto epoll_context::scheduler::io_object::cancel() -> void {
        if (fd_ == -1) return;
        COIO_ASSERT(data_ != nullptr);
        auto& context = ctx_.get();
        const auto ops = [this]{
            std::scoped_lock _{data_->fd_lock};
            return std::array{
                std::exchange(data_->in_op, nullptr),
                std::exchange(data_->out_op, nullptr)
            };
        }();
        const std::size_t n = context.op_queue_.bulk_enqueue(ops |
            std::views::filter(std::identity{}) |
            std::views::transform([](auto p) noexcept -> auto& { return *p; })
        );
        if (n > 0) context.interrupt();
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
        if (work_count_ == 0) return false;

        ::epoll_event ready_events[epoll_max_wait_count];
        while (work_count_ > 0) {
            if (const auto op = op_queue_.try_dequeue()) {
                op->finish();
                return true;
            }

            std::unique_lock lock{bolt_, std::try_to_lock};
            if (not lock) {
                std::this_thread::yield();
                continue;
            }

            if (work_count_ == 0) break;

            int timeout = infinite ? -1 : 0;
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
            timer_queue_.take_ready_timers(local_op_queue);
            for (int i = 0; i < ready_count; ++i) {
                const auto& [event, data] = ready_events[i];
                COIO_ASSERT(data.ptr != nullptr);
                if (data.ptr == &interrupter_) {
                    interrupter_.reset();
                    continue;
                }

                const auto fd_data = static_cast<epoll_data*>(data.ptr);
                const auto ops = [&]{
                    std::scoped_lock _{fd_data->fd_lock};
                    return std::array{
                        //  TODO: event & EPOLLPRI, handle EPOLLPRI for out-of-band data
                        event & EPOLLIN ? std::exchange(fd_data->in_op, nullptr) : nullptr,
                        event & EPOLLOUT ? std::exchange(fd_data->out_op, nullptr) : nullptr
                    };
                }();

                for (auto op : ops) {
                    if (op == nullptr) continue;
                    op->next_ = nullptr;
                    op->perform();
                    local_op_queue.unsynchronized_enqueue(*op);
                }
            }

            lock.unlock();
            op_queue_.splice(std::move(local_op_queue));

            if (not infinite) {
                const auto op = op_queue_.try_dequeue();
                if (op) op->finish();
                return op != nullptr;
            }
        }
        return false;
    }

    auto epoll_context::new_epoll_data() -> epoll_data* {
        return allocator_.new_object<epoll_data>();
    }

    auto epoll_context::reclaim_epoll_data(epoll_data* data) noexcept -> void {
        if (data == nullptr) return;
        return allocator_.delete_object(data);
    }

    auto epoll_context::cancel_op(int event, epoll_node* op) -> void {
        COIO_ASSERT(op != nullptr and op->data != nullptr);
        std::unique_lock fd_lock{op->data->fd_lock};
        const auto registered_op = event == EPOLLIN ?
            std::exchange(op->data->in_op, nullptr) :
            std::exchange(op->data->out_op, nullptr);

        if (registered_op != nullptr) {
            COIO_ASSERT(op == registered_op);  // if there is a registered operation, it shall be `op`
            fd_lock.unlock();
            op->immediately_post();
        }
    }

    namespace detail {
        /// async_read_some
        template<>
        auto epoll_state_base_for<async_read_some_t>::do_start() noexcept -> bool {
            if (not register_event(EPOLLIN, 0)) {
                result.set_error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_read_some_t>::do_perform() noexcept -> void {
            const ::ssize_t n = ::read(fd, buffer.data(), buffer.size());
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
        }

        template<>
        auto epoll_state_base_for<async_read_some_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_write_some
        template<>
        auto epoll_state_base_for<async_write_some_t>::do_start() noexcept -> bool {
            if (not register_event(EPOLLOUT, 0)) {
                result.set_error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_write_some_t>::do_perform() noexcept -> void {
            const ::ssize_t n = ::write(fd, buffer.data(), buffer.size());
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
        }

        template<>
        auto epoll_state_base_for<async_write_some_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        /// async_receive
        template<>
        auto epoll_state_base_for<async_receive_t>::do_start() noexcept -> bool {
            const ::ssize_t n = ::recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLIN, EPOLLET)) {
                        result.set_error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
            immediately_post();
            return true;
        }

        template<>
        auto epoll_state_base_for<async_receive_t>::do_perform() noexcept -> void {
            const ::ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
        }

        template<>
        auto epoll_state_base_for<async_receive_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_send
        template<>
        auto epoll_state_base_for<async_send_t>::do_start() noexcept -> bool {
            const ::ssize_t n = ::send(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLOUT, EPOLLET)) {
                        result.set_error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
            immediately_post();
            return true;
        }

        template<>
        auto epoll_state_base_for<async_send_t>::do_perform() noexcept -> void {
            const ::ssize_t n = ::send(fd, buffer.data(), buffer.size(), MSG_NOSIGNAL);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
        }

        template<>
        auto epoll_state_base_for<async_send_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        /// async_receive_from
        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_start() noexcept -> bool {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            const ::ssize_t n = ::recvfrom(fd, buffer.data(), buffer.size(), MSG_DONTWAIT, psa, &len);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLIN, EPOLLET)) {
                        result.set_error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
            immediately_post();
            return true;
        }

        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_perform() noexcept -> void {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            const ::ssize_t n = ::recvfrom(fd, buffer.data(), buffer.size(), MSG_DONTWAIT, psa, &len);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
        }

        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_send_to
        template<>
        auto epoll_state_base_for<async_send_to_t>::do_start() noexcept -> bool {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::sendto(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL, psa, len);
            if (n == -1) {
                if (is_blocking_errno(errno)) {
                    if (not register_event(EPOLLOUT, EPOLLET)) {
                        result.set_error(std::error_code(errno, std::system_category()));
                        return false;
                    }
                    return true;
                }
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
            immediately_post();
            return true;
        }

        template<>
        auto epoll_state_base_for<async_send_to_t>::do_perform() noexcept -> void {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::sendto(fd, buffer.data(), buffer.size(), MSG_NOSIGNAL, psa, len);
            if (n == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code(errno, std::system_category()));
            }
            else {
                result.set_value(n);
            }
        }

        template<>
        auto epoll_state_base_for<async_send_to_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        /// async_accept
        template<>
        auto epoll_state_base_for<async_accept_t>::do_start() noexcept -> bool {
            if (not register_event(EPOLLIN, 0)) {
                result.set_error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_accept_t>::do_perform() noexcept -> void {
            auto accepted_ = ::accept4(fd, nullptr, nullptr, 0);
            if (accepted_ == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(accepted_);
            }
        }

        template<>
        auto epoll_state_base_for<async_accept_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_connect
        template<>
        auto epoll_state_base_for<async_connect_t>::do_start() noexcept -> bool {
            if (not register_event(EPOLLOUT, 0)) {
                result.set_error(std::error_code(errno, std::system_category()));
                return false;
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_connect_t>::do_perform() noexcept -> void {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = detail::to_sockaddr(sa);
            if (::connect(fd, psa, len) == -1) {
                COIO_ASSERT(not is_blocking_errno(errno));
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value();
            }
        }

        template<>
        auto epoll_state_base_for<async_connect_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }
    }
}
#endif