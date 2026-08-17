#include <coio/detail/config.h>
#if COIO_HAS_IO_URING
#include <limits>
#include <coio/asyncio/uring_context.h>
#include <coio/utils/scope_exit.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep
#include "../common.h"

namespace coio {
    namespace {
        constexpr std::size_t default_uring_entries = 4096;

        constexpr std::size_t submit_batch_size = 32;

        auto init_uring(::io_uring& uring, std::size_t entries) -> void {
            if (entries > std::numeric_limits<unsigned>::max()) {
                throw std::system_error{std::make_error_code(std::errc::value_too_large)};
            }
            if (const auto ec = ::io_uring_queue_init(entries, &uring, 0u); ec < 0) {
                throw std::system_error{-ec, std::system_category()};
            }
            // IORING_OP_SOCKET (opcode 45) shipped in Linux 5.19 together with
            // IORING_ASYNC_CANCEL_FD/ALL, which fd-scoped cancellation relies on; probing it
            // also implies IORING_FEAT_EXT_ARG (5.11), required for the lock-free CQ wait
            ::io_uring_probe probe{};
            if (::io_uring_register_probe(&uring, &probe, 0) < 0 or probe.last_op < ::IORING_OP_SOCKET) {
                ::io_uring_queue_exit(&uring);
                throw std::system_error{
                    std::make_error_code(std::errc::operation_not_supported),
                    "coio::uring_context requires Linux kernel 5.19 or newer"
                };
            }
        }

        [[noreturn]]
        auto sqe_exhuasted() -> void {
            // TODO: more proper handle
            std::terminate();
        }
    }

    auto uring_context::uring_node::do_cancel() -> void {
        std::scoped_lock _{context_.uring_mtx_};
        auto sqe = context_.allocate_sqe();
        if (sqe == nullptr) [[unlikely]] {
            sqe_exhuasted();
        }
        ::io_uring_prep_cancel(sqe, this, 0);
        ::io_uring_sqe_set_data(sqe, nullptr);
        context_.submit_sqes();
    }

    uring_context::scheduler::io_object::io_object(uring_context& ctx, int fd) : ctx_(&ctx), fd_(fd), stream_oriented_(detail::is_stream_oriented_(fd)) {}

    uring_context::scheduler::io_object::~io_object() {
        close();
    }

    auto uring_context::scheduler::io_object::close() -> void {
        if (fd_ == -1) return;
        detail::throw_last_error(::close(std::exchange(fd_, -1)), "close");
    }

    auto uring_context::scheduler::io_object::cancel() -> void {
        if (fd_ == -1) return;
        std::scoped_lock _{ctx_->uring_mtx_};
        auto sqe = ctx_->allocate_sqe();
        if (sqe == nullptr) [[unlikely]] {
            sqe_exhuasted();
        }
        ::io_uring_prep_cancel_fd(sqe, fd_, IORING_ASYNC_CANCEL_ALL);
        ::io_uring_sqe_set_data(sqe, nullptr);
        ctx_->submit_sqes();
    }

    auto uring_context::scheduler::io_object::receive(std::span<std::byte> buffer) -> std::size_t {
        return detail::socket::receive(fd_, buffer, stream_oriented_);
    }

    auto uring_context::scheduler::io_object::send(std::span<const std::byte> buffer) -> std::size_t {
        return detail::socket::send(fd_, buffer);
    }

    auto uring_context::scheduler::io_object::receive_from(std::span<std::byte> buffer) -> std::pair<endpoint, std::size_t> {
        return detail::socket::receive_from(fd_, buffer);
    }

    auto uring_context::scheduler::io_object::send_to(std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t {
        return detail::socket::send_to(fd_, buffer, dest);
    }

    auto uring_context::scheduler::io_object::connect(const endpoint& peer) -> void {
        detail::socket::connect(fd_, peer);
    }

    auto uring_context::scheduler::io_object::accept() -> detail::socket_native_handle_type {
        return detail::socket::accept(fd_);
    }

    auto uring_context::scheduler::io_object::read_some(std::span<std::byte> buffer) -> std::size_t {
        return detail::file_read(fd_, buffer);
    }

    auto uring_context::scheduler::io_object::write_some(std::span<const std::byte> buffer) -> std::size_t {
        return detail::file_write(fd_, buffer);
    }

    auto uring_context::scheduler::io_object::read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t {
        return detail::file_read_at(fd_, offset, buffer);
    }

    auto uring_context::scheduler::io_object::write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t {
        return detail::file_write_at(fd_, offset, buffer);
    }

    auto uring_context::scheduler::io_object::seek(std::size_t offset, detail::seek_whence whence) -> std::size_t {
        return detail::file_seek(fd_, offset, whence);
    }

    auto uring_context::scheduler::io_object::resize(std::size_t new_size) -> void {
        detail::file_resize(fd_, new_size);
    }

    auto uring_context::scheduler::make_io_object(int fd) const -> io_object try {
        return io_object{*ctx_, fd};
    }
    catch (...) {
        ::close(fd);
        throw;
    }

    uring_context::uring_context(std::size_t entries, std::pmr::memory_resource& memory_resource) : loop_base(memory_resource) {
        init_uring(uring_, entries);
    }

    uring_context::uring_context() : uring_context(default_uring_entries) {}

    uring_context::~uring_context() {
        ::io_uring_queue_exit(&uring_);
    }

    auto uring_context::do_one(bool infinite) -> bool {
        if (work_count_ == 0) return false;

        while (work_count_ > 0) {
            if (consume()) {
                return true;
            }

            pulling_cqes_.store(true, std::memory_order_release);
            scope_exit flag_guard{[this]() noexcept {
                pulling_cqes_.store(false, std::memory_order_release);
            }};

            if (work_count_ == 0) break;

            uring_mtx_.lock();
            submit_sqes(); // nothrow
            uring_mtx_.unlock();

            detail::intrusive_list<node> ready_io_ops{&node::next_};
            ::io_uring_cqe* cqe = nullptr;
            scope_exit cqe_guard{[&] {
                ::io_uring_cqe_seen(&uring_, cqe);
            }};
            int ec = 0;
            if (infinite) {
                using microseconds = std::chrono::duration<std::int64_t, std::micro>;
                if (const auto earliest = timer_queue_.earliest()) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto usec = std::max(std::chrono::ceil<microseconds>(*earliest - now).count(), {});
                    ::__kernel_timespec timeout{
                        .tv_sec = usec / 1000'000,
                        .tv_nsec = (usec % 1000'000) * 1'000
                    };
                    ec = -::io_uring_wait_cqe_timeout(&uring_, &cqe, &timeout);
                }
                else {
                    ec = -::io_uring_wait_cqe_timeout(&uring_, &cqe, nullptr);
                }
            }
            else {
                ::__kernel_timespec immediate{};
                ec = -::io_uring_wait_cqe_timeout(&uring_, &cqe, &immediate);
            }

            if (ec == EINTR or ec == ETIME) {
                cqe_guard.reset();
                cqe = nullptr;
                ec = 0;
            }
            if (ec > 0) throw std::system_error{ec, std::system_category()};

            if (cqe) {
                if (auto user_data = ::io_uring_cqe_get_data(cqe); user_data and user_data != this) {
                    auto op = static_cast<uring_node*>(user_data);
                    COIO_TSAN_ACQUIRE(op);
                    op->complete(cqe->res);
                    ready_io_ops.push_back(*op);
                }
            }
            cqe_guard.reset();

            detail::intrusive_list<node> ready_time_ops{&node::next_};
            timer_queue_.take_ready_timers(ready_time_ops);

            while (true) {
                ::io_uring_cqe* peeked_cqes[8]{};
                const auto n = ::io_uring_peek_batch_cqe(&uring_, peeked_cqes, std::ranges::size(peeked_cqes));
                if (n == 0) break;
                scope_exit _{[this, n]() noexcept {
                    ::io_uring_cq_advance(&uring_, n);
                }};
                for (auto peeked_cqe : std::span(peeked_cqes, n)) {
                    if (auto user_data = ::io_uring_cqe_get_data(peeked_cqe); user_data and user_data != this) {
                        auto op = static_cast<uring_node*>(user_data);
                        COIO_TSAN_ACQUIRE(op);
                        op->complete(peeked_cqe->res);
                        ready_io_ops.push_back(*op);
                    }
                }
            }

            flag_guard.reset();

            publish_pending(ready_time_ops.release());
            publish_pending(ready_io_ops.release());

            if (not infinite) {
                return consume();
            }
        }
        return false;
    }

    auto uring_context::allocate_sqe() noexcept -> io_uring_sqe* {
        ::io_uring_sqe* sqe = ::io_uring_get_sqe(&uring_);
        for (std::size_t retry = 3u; sqe == nullptr and retry-- > 0;) {
            submit_sqes();
            sqe = ::io_uring_get_sqe(&uring_);
        }
        if (sqe) ++pending_sqes_;
        return sqe;
    }

    auto uring_context::submit_sqes() noexcept -> void { // pre: uring_mtx_ is locked
        if (pending_sqes_ == 0) return;
        const int n = ::io_uring_submit(&uring_);
        if (n < 0) [[unlikely]] std::terminate();
        COIO_ASSERT(pending_sqes_ >= std::size_t(n)); // NOLINT(*-use-integer-sign-comparison)
        pending_sqes_ -= n;
    }

    auto uring_context::post_submit_sqes() noexcept -> void { // pre: uring_mtx_ is locked
        if (not pulling_cqes_.load(std::memory_order_acquire)) {
            if (pending_sqes_ < submit_batch_size) return;
            submit_sqes();
        }
        else {
            submit_sqes();
        }
    }

    auto uring_context::interrupt() -> void {
        std::scoped_lock _{uring_mtx_};
        auto sqe = allocate_sqe();
        if (sqe == nullptr) [[unlikely]] {
            sqe_exhuasted();
        }
        ::io_uring_prep_nop(sqe);
        ::io_uring_sqe_set_data(sqe, this);
        submit_sqes();
    }

    namespace detail {
        /// async_read_some
        auto uring_state_base_for<read_some_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_read(sqe, fd, buffer_.data(), buffer_.size(), -1);
        }

        auto uring_state_base_for<read_some_tag>::complete(int cqe_res) noexcept -> void {
            if (cqe_res < 0) {
                const std::error_code ec{-cqe_res, std::system_category()};
                if (ec == std::errc::operation_canceled) {
                    result.set_stopped();
                }
                else {
                    result.set_error(ec);
                }
            }
            else {
                if (cqe_res == 0 and not buffer_.empty()) [[unlikely]] {
                    result.set_error(error::eof);
                }
                else {
                    result.set_value(cqe_res);
                }
            }
        }


        /// async_write_some
        auto uring_state_base_for<write_some_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_write(sqe, fd, buffer_.data(), buffer_.size(), -1);
        }


        /// async_read_some_at
        auto uring_state_base_for<read_some_at_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_read(sqe, fd, buffer_.data(), buffer_.size(), offset_);
        }

        auto uring_state_base_for<read_some_at_tag>::complete(int cqe_res) noexcept -> void {
            if (cqe_res < 0) {
                const std::error_code ec{-cqe_res, std::system_category()};
                if (ec == std::errc::operation_canceled) {
                    result.set_stopped();
                }
                else {
                    result.set_error(ec);
                }
            }
            else {
                if (cqe_res == 0 and not buffer_.empty()) [[unlikely]] {
                    result.set_error(error::eof);
                }
                else {
                    result.set_value(cqe_res);
                }
            }
        }

        /// async_write_some_at
        auto uring_state_base_for<write_some_at_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_write(sqe, fd, buffer_.data(), buffer_.size(), offset_);
        }


        /// async_receive
        auto uring_state_base_for<receive_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_recv(sqe, fd, buffer_.data(), buffer_.size(), 0);
        }

        auto uring_state_base_for<receive_tag>::try_complete() noexcept -> bool {
            if (not stream_oriented_ or not buffer_.empty()) return false;
            result.set_value(0);
            return true;
        }

        auto uring_state_base_for<receive_tag>::complete(int cqe_res) noexcept -> void {
            if (cqe_res < 0) {
                const std::error_code ec{-cqe_res, std::system_category()};
                if (ec == std::errc::operation_canceled) {
                    result.set_stopped();
                }
                else {
                    result.set_error(ec);
                }
            }
            else {
                if (stream_oriented_ and cqe_res == 0 and not buffer_.empty()) [[unlikely]] {
                    result.set_error(error::eof);
                }
                else {
                    result.set_value(cqe_res);
                }
            }
        }

        /// async_send
        auto uring_state_base_for<send_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_send(sqe, fd, buffer_.data(), buffer_.size(), MSG_NOSIGNAL);
        }


        /// async_receive_from
        uring_state_base_for<receive_from_tag>::uring_state_base_for(int fd, uring_context& context, std::span<std::byte> buffer) noexcept :
            uring_node_for(fd, context) {
            buffer_ = {
                .iov_base = buffer.data(),
                .iov_len = buffer.size()
            };
            msg_ = {
                .msg_name = &peer_,
                .msg_namelen = sizeof(peer_),
                .msg_iov = &buffer_,
                .msg_iovlen = 1
            };
        }

        auto uring_state_base_for<receive_from_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_recvmsg(sqe, fd, &msg_, 0);
        }

        auto uring_state_base_for<receive_from_tag>::complete(int cqe_res) noexcept -> void {
            if (cqe_res < 0) {
                const std::error_code ec{-cqe_res, std::system_category()};
                if (ec == std::errc::operation_canceled) {
                    result.set_stopped();
                }
                else {
                    result.set_error(ec);
                }
            }
            else {
                result.set_value(sockaddr_storage_to_endpoint(peer_), cqe_res);
            }
        }


        /// async_send_to
        uring_state_base_for<send_to_tag>::uring_state_base_for(int fd, uring_context& context, std::span<const std::byte> buffer, const endpoint& dest) noexcept :
            uring_node_for(fd, context),
            peer_(endpoint_to_sockaddr_in(dest)) {
            auto [psa, len] = to_sockaddr(peer_);
            buffer_ = {
                .iov_base = const_cast<std::byte*>(buffer.data()),
                .iov_len = buffer.size()
            };
            msg_ = {
                .msg_name = psa,
                .msg_namelen = len,
                .msg_iov = &buffer_,
                .msg_iovlen = 1
            };
        }

        auto uring_state_base_for<send_to_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_sendmsg(sqe, fd, &msg_, MSG_NOSIGNAL);
        }


        /// async_accept
        auto uring_state_base_for<accept_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            ::io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
        }


        /// async_connect
        uring_state_base_for<connect_tag>::uring_state_base_for(int fd, uring_context& context, const endpoint& peer) noexcept :
            uring_node_for(fd, context),
            peer_(endpoint_to_sockaddr_in(peer)) {}

        auto uring_state_base_for<connect_tag>::prepare(::io_uring_sqe* sqe) noexcept -> void {
            auto [psa, len] = to_sockaddr(peer_);
            ::io_uring_prep_connect(sqe, fd, psa, len);
        }
    }
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep

#endif
