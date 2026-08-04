// ReSharper disable CppMemberFunctionMayBeConst
#include <coio/detail/config.h>
#if COIO_HAS_EPOLL
#include <algorithm>
#include <ranges>
#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <coio/asyncio/epoll_context.h>
#include "../common.h"

namespace coio {
    namespace detail {
        namespace {
            constexpr int epoll_max_wait_count = 128;
        }

        reactor_interrupter::reactor_interrupter() {
            reader_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (reader_ != -1) [[likely]] {
                writer_ = reader_;
                return;
            }
            // fallback: use pipe
            int pipedes[2];
            detail::throw_last_error(::pipe2(pipedes, O_CLOEXEC | O_NONBLOCK));
            reader_ = pipedes[0];
            writer_ = pipedes[1];
        }

        reactor_interrupter::~reactor_interrupter() {
            no_errno_here(::close(reader_));
            if (writer_ != reader_) [[unlikely]] {
                no_errno_here(::close(writer_));
            }
        }

        auto reactor_interrupter::interrupt() -> void {
            static constexpr std::uint64_t data = 1;
            static_cast<void>(::write(writer_, &data, sizeof(data)));
        }

        auto reactor_interrupter::reset() -> bool {
            if (writer_ == reader_) [[likely]] {
                while (true) {
                    std::uint64_t data;
                    const auto n = ::read(reader_, &data, sizeof(data));
                    if (n < 0) [[unlikely]] {
                        if (errno == EINTR) continue;
                        return false;
                    }
                    return true;
                }
            }

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

        namespace {
            auto create_epoll() -> int {
                const auto fd = ::epoll_create1(EPOLL_CLOEXEC);
                detail::throw_last_error(fd);
                return fd;
            }
        }
    }

    auto epoll_context::epoll_node::register_event(int event_type) noexcept -> register_result {
        std::scoped_lock _{data->fd_lock};
        const bool in_op_registered = data->in_op;
        const bool out_op_registered = data->out_op;
        if (event_type == EPOLLIN /* or event_type == EPOLLPRI */) {
            COIO_ASSERT(not in_op_registered && "an asynchronous input operation shall be initiated after another input operation has completed.");
        }
        else if (event_type == EPOLLOUT) {
            COIO_ASSERT(not out_op_registered && "an asynchronous output operation shall be initiated after another output operation has completed.");
        }
        else unreachable();

        // under EPOLLET an edge reported while no op claims it is recorded by do_one into
        // `ready_events`; consume it here and make the caller retry the I/O, otherwise the
        // edge would be lost forever whenever the unchanged mask lets us skip epoll_ctl below
        const std::uint32_t interest = event_type | EPOLLERR | EPOLLHUP;
        if (data->ready_events & interest) {
            data->ready_events &= ~interest;
            return register_result::ready;
        }

        std::uint32_t ev = event_type | EPOLLET;
        int epoll_ctl_op = data->events == 0 ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

        if (in_op_registered) {
            ev |= EPOLLIN;
            epoll_ctl_op = EPOLL_CTL_MOD;
        }
        if (out_op_registered) {
            ev |= EPOLLOUT;
            epoll_ctl_op = EPOLL_CTL_MOD;
        }

        bool ok = ev == data->events;
        if (not ok) {
            ::epoll_event event{.events = ev, .data = {.ptr = data}};
            ok = ::epoll_ctl(context_.epoll_fd_, epoll_ctl_op, fd, &event) == 0;
        }
        if (ok) [[likely]] {
            data->events = ev;
            if (event_type == EPOLLIN /* or event_type == EPOLLPRI */) {
                data->in_op = this;
            }
            else if (event_type == EPOLLOUT) {
                data->out_op = this;
            }
            return register_result::armed;
        }
        return register_result::failure;
    }


    epoll_context::scheduler::io_object::io_object(epoll_context& ctx, int fd) try : ctx_(ctx), fd_(fd) {
        if (fd == -1) return;
        struct ::stat st{};
        if (::fstat(fd, &st) == -1) [[unlikely]] {
            throw std::system_error{errno, std::system_category(), "fstat"};
        }
        if (S_ISREG(st.st_mode) or S_ISDIR(st.st_mode)) [[unlikely]] {
            throw std::system_error{
                std::make_error_code(std::errc::operation_not_permitted),
                "the target file `fd` doesn't support epoll"
            };
        }
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags == -1) [[unlikely]] {
            throw std::system_error{errno, std::system_category(), "fcntl(fd, F_GETFL)"};
        }
        if ((flags & O_NONBLOCK) == 0) {
            if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) [[unlikely]] {
                throw std::system_error{errno, std::system_category(), "fcntl(fd, F_SETFL, ...)"};
            }
        }
        data_ = ctx_.get().new_epoll_data();
    }
    catch (...) {
        ::close(fd);
        throw;
    }

    epoll_context::scheduler::io_object::~io_object() {
        cancel();
        // contract: an io_object whose fd is still registered with epoll must be release()d before destruction
        COIO_ASSERT(data_ == nullptr or data_->events == 0);
        ctx_.get().reclaim_epoll_data(data_);
    }

    auto epoll_context::scheduler::io_object::release() -> int {
        if (fd_ == -1) return -1;
        COIO_ASSERT(data_ != nullptr);
        epoll_context& context = ctx_;
        cancel();
        {
            std::scoped_lock _{data_->fd_lock};
            if (data_->events != 0) {
                static_cast<void>(::epoll_ctl(context.epoll_fd_, EPOLL_CTL_DEL, fd_, nullptr));
                data_->events = 0;
            }
        }
        context.reclaim_epoll_data(std::exchange(data_, nullptr));
        return std::exchange(fd_, -1);
    }

    auto epoll_context::scheduler::io_object::cancel() -> void {
        if (fd_ == -1) return;
        COIO_ASSERT(data_ != nullptr);
        const auto ops = [this]{
            std::scoped_lock _{data_->fd_lock};
            return std::array{
                std::exchange(data_->in_op, nullptr),
                std::exchange(data_->out_op, nullptr)
            };
        }();
        for (auto op : ops) {
            if (op != nullptr) op->publish();
        }
    }

    epoll_context::epoll_context(std::pmr::memory_resource& memory_resource) :
        loop_base(memory_resource), data_pool_(&memory_resource), epoll_fd_(detail::create_epoll())
    {
        ::epoll_event event {
            .events = std::uint32_t(EPOLLIN | EPOLLET),
            .data = {.ptr = &interrupter_}
        };
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, interrupter_.watcher(), &event) == -1) [[unlikely]] {
            ::close(epoll_fd_);
            throw std::system_error(errno, std::system_category());
        }
    }

    epoll_context::~epoll_context() {
        request_stop();
        ::close(epoll_fd_);
    }

    auto epoll_context::do_one(bool infinite) -> bool {
        if (work_count_ == 0) return false;

        ::epoll_event ready_events[detail::epoll_max_wait_count];
        while (work_count_ > 0) {
            if (consume()) {
                return true;
            }

            if (work_count_ == 0) break;

            int timeout = infinite ? -1 : 0;
            if (infinite) {
                if (const auto earliest = timer_queue_.earliest()) {
                    const auto now = std::chrono::steady_clock::now();
                    auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(*earliest - now).count();
                    if (msec > 0) msec += 1; // round up so we never wake just before the deadline and spin
                    timeout = static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(msec, 0, std::numeric_limits<int>::max()));
                }
            }
            const int ready_count = ::epoll_wait(epoll_fd_, ready_events, detail::epoll_max_wait_count, timeout);
            if (ready_count == -1 and errno == EINTR) continue;
            detail::throw_last_error(ready_count, "epoll_wait");

            detail::intrusive_list<node> ready_time_ops{&node::next_}, ready_io_ops{&node::next_};
            timer_queue_.take_ready_timers(ready_time_ops);

            for (int i = 0; i < ready_count; ++i) {
                const auto& [event, data] = ready_events[i];
                COIO_ASSERT(data.ptr != nullptr);
                if (data.ptr == &interrupter_) {
                    interrupter_.reset();
                    continue;
                }
                const auto fd_data = static_cast<per_fd_data*>(data.ptr);
                std::scoped_lock _{fd_data->fd_lock};
                std::array ops{
                    std::pair{EPOLLIN, std::ref(fd_data->in_op)},
                    std::pair{EPOLLOUT, std::ref(fd_data->out_op)}
                    // TODO: handle EPOLLPRI for out-of-band data
                };
                for (auto [ev, op_ref] : ops) {
                    auto& op = op_ref.get();
                    if (event & (ev | EPOLLERR | EPOLLHUP)) {
                        if (op == nullptr) {
                            fd_data->ready_events |= event & (ev | EPOLLERR | EPOLLHUP);
                            continue;
                        }
                        if (not op->perform()) continue;
                        ready_io_ops.push_back(*op);
                        op = nullptr;
                    }
                }
            }

            publish_pending(ready_time_ops.release());
            publish_pending(ready_io_ops.release());

            if (not infinite) {
                return consume();
            }
        }
        return false;
    }

    auto epoll_context::new_epoll_data() -> per_fd_data* {
        const auto data = data_pool_.acquire();
        // serialize with straggling stale accessors before recycling the entry
        std::scoped_lock _{data->fd_lock};
        data->events = 0;
        data->ready_events = 0;
        data->in_op = nullptr;
        data->out_op = nullptr;
        data->next_free = nullptr;
        return data;
    }

    auto epoll_context::reclaim_epoll_data(per_fd_data* data) noexcept -> void {
        if (data == nullptr) return;
        data_pool_.release(*data);
    }

    auto epoll_context::cancel_op(int event, epoll_node* op) -> void {
        COIO_ASSERT(op != nullptr and op->data != nullptr);
        std::unique_lock fd_lock{op->data->fd_lock};
        auto& registered_op = event == EPOLLIN ? op->data->in_op : op->data->out_op;
        // `data` may have been recycled for another fd, in which case the slot belongs
        // to someone else's operation: only clear it when it is still ours
        if (registered_op == op) {
            registered_op = nullptr;
            fd_lock.unlock();
            op->publish();
        }
    }

    namespace detail {
        /// async_read_some
        template<>
        auto epoll_state_base_for<async_read_some_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                return start_result::completed;
            }
            while (true) {
                const ::ssize_t n = ::read(fd, buffer.data(), buffer.size());
                if (n == -1) {
                    if (is_blocking_errno(errno)) {
                        switch (register_event(EPOLLIN)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                result.set_value(n);
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_read_some_t>::do_perform() noexcept -> bool {
            const ::ssize_t n = ::read(fd, buffer.data(), buffer.size());
            if (n == -1) {
                if (is_blocking_errno(errno)) [[unlikely]] {
                    return false;
                }
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(n);
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_read_some_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_write_some
        template<>
        auto epoll_state_base_for<async_write_some_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                return start_result::completed;
            }
            while (true) {
                const ::ssize_t n = ::write(fd, buffer.data(), buffer.size());
                if (n == -1) {
                    if (is_blocking_errno(errno)) {
                        switch (register_event(EPOLLOUT)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                result.set_value(n);
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_write_some_t>::do_perform() noexcept -> bool {
            const ::ssize_t n = ::write(fd, buffer.data(), buffer.size());
            if (n == -1) {
                if (is_blocking_errno(errno)) [[unlikely]] {
                    return false;
                }
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(n);
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_write_some_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        /// async_receive
        template<>
        auto epoll_state_base_for<async_receive_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            while (true) {
                const ::ssize_t n = ::recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
                if (n == -1) {
                    if (is_blocking_errno(errno)) {
                        switch (register_event(EPOLLIN)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                result.set_value(n);
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_receive_t>::do_perform() noexcept -> bool {
            const ::ssize_t n = ::recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (n == -1) {
                if (is_blocking_errno(errno)) [[unlikely]] {
                    return false;
                }
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(n);
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_receive_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_send
        template<>
        auto epoll_state_base_for<async_send_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            while (true) {
                const ::ssize_t n = ::send(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n == -1) {
                    if (is_blocking_errno(errno)) {
                        switch (register_event(EPOLLOUT)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                result.set_value(n);
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_send_t>::do_perform() noexcept -> bool {
            const ::ssize_t n = ::send(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n == -1) {
                if (is_blocking_errno(errno)) [[unlikely]] {
                    return false;
                }
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(n);
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_send_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        /// async_receive_from
        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            while (true) {
                ::socklen_t len = sizeof(peer);
                const ::ssize_t n = ::recvfrom(
                    fd, buffer.data(), buffer.size(), MSG_DONTWAIT,
                    reinterpret_cast<::sockaddr*>(&peer), &len
                );
                if (n == -1) {
                    if (is_blocking_errno(errno)) {
                        switch (register_event(EPOLLIN)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                result.set_value(sockaddr_storage_to_endpoint(peer), n);
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_perform() noexcept -> bool {
            ::socklen_t len = sizeof(peer);
            const ::ssize_t n = ::recvfrom(
                fd, buffer.data(), buffer.size(), MSG_DONTWAIT,
                reinterpret_cast<::sockaddr*>(&peer), &len
            );
            if (n == -1) {
                if (is_blocking_errno(errno)) [[unlikely]] {
                    return false;
                }
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(sockaddr_storage_to_endpoint(peer), n);
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_send_to
        template<>
        auto epoll_state_base_for<async_send_to_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            while (true) {
                auto sa = endpoint_to_sockaddr_in(peer);
                auto [psa, len] = to_sockaddr(sa);
                ::ssize_t n = ::sendto(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL, psa, len);
                if (n == -1) {
                    if (is_blocking_errno(errno)) {
                        switch (register_event(EPOLLOUT)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                result.set_value(n);
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_send_to_t>::do_perform() noexcept -> bool {
            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            ::ssize_t n = ::sendto(fd, buffer.data(), buffer.size(), MSG_DONTWAIT | MSG_NOSIGNAL, psa, len);
            if (n == -1) {
                if (is_blocking_errno(errno)) [[unlikely]] {
                    return false;
                }
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(n);
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_send_to_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }


        /// async_accept
        template<>
        auto epoll_state_base_for<async_accept_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            while (true) {
                const int accepted = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK);
                if (accepted == -1) {
                    if (is_blocking_errno(errno)) {
                        switch (register_event(EPOLLIN)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                result.set_value(accepted);
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_accept_t>::do_perform() noexcept -> bool {
            auto accepted_ = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK);
            if (accepted_ == -1) {
                if (is_blocking_errno(errno)) [[unlikely]] {
                    return false;
                }
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else {
                result.set_value(accepted_);
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_accept_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLIN, this);
        }


        /// async_connect
        template<>
        auto epoll_state_base_for<async_connect_t>::do_start() noexcept -> start_result {
            if (fd == -1) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            bool in_progress = false;
            while (true) {
                const auto flags = ::fcntl(fd, F_GETFL);
                if (flags == -1) [[unlikely]] {
                    result.set_error(std::error_code{errno, std::system_category()});
                    return start_result::completed;
                }
                if ((flags & O_NONBLOCK) == 0) {
                    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) [[unlikely]] {
                        result.set_error(std::error_code{errno, std::system_category()});
                        return start_result::completed;
                    }
                }
                auto sa = endpoint_to_sockaddr_in(peer);
                auto [psa, len] = to_sockaddr(sa);
                const int ec = ::connect(fd, psa, len) == -1 ? errno : 0;
                if ((flags & O_NONBLOCK) == 0) {
                    if (::fcntl(fd, F_SETFL, flags) == -1) [[unlikely]] {
                        result.set_error(std::error_code{errno, std::system_category()});
                        return start_result::completed;
                    }
                }
                if (ec != 0) {
                    // EISCONN on a retry means the connection we initiated completed in the meantime
                    if (ec == EISCONN and in_progress) {
                        result.set_value();
                        return start_result::completed;
                    }
                    // a retried ::connect while the connection is still in progress reports EALREADY
                    if (ec == EINPROGRESS or ec == EALREADY or ec == EAGAIN) {
                        in_progress = true;
                        switch (register_event(EPOLLOUT)) {
                        case register_result::armed:
                            return start_result::pending;
                        case register_result::ready:
                            continue; // consume a previously skipped edge, retry the I/O
                        case register_result::failure:
                            result.set_error(std::error_code{errno, std::system_category()});
                            return start_result::completed;
                        }
                    }
                    result.set_error(std::error_code{ec, std::system_category()});
                    return start_result::completed;
                }
                result.set_value();
                return start_result::completed;
            }
        }

        template<>
        auto epoll_state_base_for<async_connect_t>::do_perform() noexcept -> bool {
            // SO_ERROR reads 0 both when connected and while the handshake is still in
            // progress, so a stale event delivered to a recycled per_fd_data entry could
            // complete the connect prematurely; ask a zero-timeout poll whether the
            // connect has resolved at all before trusting the wakeup
            ::pollfd pfd{.fd = fd, .events = POLLOUT, .revents = 0};
            if (::poll(&pfd, 1, 0) == 0) [[unlikely]] {
                return false;
            }
            int ec = 0;
            ::socklen_t len = sizeof(ec);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &ec, &len) == -1) {
                result.set_error(std::error_code{errno, std::system_category()});
            }
            else if (ec != 0) {
                result.set_error(std::error_code{ec, std::system_category()});
            }
            else {
                result.set_value();
            }
            return true;
        }

        template<>
        auto epoll_state_base_for<async_connect_t>::do_cancel() -> void {
            context_.cancel_op(EPOLLOUT, this);
        }
    }
}
#endif
