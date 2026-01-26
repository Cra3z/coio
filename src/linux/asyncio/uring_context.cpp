// #include <coio/detail/config.h>
// #if COIO_HAS_IO_URING
// #include <limits>
// #include <coio/asyncio/uring_context.h>
// #include <coio/utils/scope_exit.h>
// #include "../common.h"
//
// namespace coio {
//     namespace {
//         constexpr std::size_t default_uring_entries = 1024;
//
//         auto throw_uring_error(int ec) -> void {
//             if (ec < 0) throw std::system_error{-ec, std::system_category()};
//         }
//
//         auto init_uring(::io_uring& uring, std::size_t entries) -> void {
//             if (entries > std::numeric_limits<unsigned>::max()) {
//                 throw std::system_error{std::make_error_code(std::errc::value_too_large)};
//             }
//             if (const auto ec = ::io_uring_queue_init(entries, &uring, 0u); ec < 0) {
//                 throw std::system_error{-ec, std::system_category()};
//             }
//         }
//     }
//
//     auto uring_context::uring_op_base::cancel() -> void {
//         std::scoped_lock _{context_.mtx_};
//         auto sqe = context_.allocate_sqe();
//         if (sqe == nullptr) {
//             throw std::system_error{std::make_error_code(std::errc::no_buffer_space)};
//         }
//         ::io_uring_prep_cancel(sqe, this, 0);
//         throw_uring_error(::io_uring_submit(&context_.uring_));
//     }
//
//     auto uring_context::scheduler::io_object::release() -> int {
//         cancel();
//         return std::exchange(fd_, -1);
//     }
//
//     auto uring_context::scheduler::io_object::cancel() -> void {
//         if (fd_ == -1) return;
//         std::scoped_lock _{ctx_->mtx_};
//         auto sqe = ctx_->allocate_sqe();
//         if (sqe == nullptr) {
//             throw std::system_error{std::make_error_code(std::errc::no_buffer_space)};
//         }
//         ::io_uring_prep_cancel_fd(sqe, fd_, IORING_ASYNC_CANCEL_ALL);
//         throw_uring_error(::io_uring_submit(&ctx_->uring_));
//     }
//
//     uring_context::uring_context(std::size_t entries, std::pmr::memory_resource& memory_resource) : loop_base(memory_resource) {
//         init_uring(uring_, entries);
//     }
//
//     uring_context::uring_context() : uring_context(default_uring_entries) {}
//
//     uring_context::~uring_context() {
//         ::io_uring_queue_exit(&uring_);
//     }
//
//     auto uring_context::do_one(bool infinite) -> bool {
//         if (stop_requested()) return false;
//
//         while (not stop_requested()) {
//             timer_queue_.take_ready_timers(op_queue_);
//             if (const auto op = op_queue_.try_dequeue()) {
//                 op->finish();
//                 return true;
//             }
//
//             std::unique_lock lock{mtx_, std::try_to_lock};
//             if (not lock.owns_lock()) continue;
//             if (stop_requested()) break;
//
//             op_queue local_op_queue;
//             ::io_uring_cqe* cqe = nullptr;
//             std::optional cqe_guard{scope_exit{[&] {
//                 ::io_uring_cqe_seen(&uring_, cqe);
//             }}};
//             int ec = 0;
//             if (infinite) {
//                 using microseconds = std::chrono::duration<std::int64_t, std::micro>;
//                 if (const auto earliest = timer_queue_.earliest()) {
//                     const auto now = std::chrono::steady_clock::now();
//                     const auto usec = std::max(std::chrono::duration_cast<microseconds>(*earliest - now).count(), {});
//                     ::__kernel_timespec timeout{
//                         .tv_sec = usec / 1000'000,
//                         .tv_nsec = (usec % 1000'000) * 1'000
//                     };
//                     ec = -::io_uring_submit_and_wait_timeout(&uring_, &cqe, 1, &timeout, nullptr);
//                 }
//                 else {
//                     ec = -::io_uring_submit_and_wait_timeout(&uring_, &cqe, 1, nullptr, nullptr);
//                 }
//             }
//             else {
//                 ::__kernel_timespec immediate{};
//                 ec = -::io_uring_submit_and_wait_timeout(&uring_, &cqe, 1, &immediate, nullptr);
//             }
//
//             if (ec == EINTR or ec == ETIME) {
//                 cqe_guard.reset();
//                 cqe = nullptr;
//                 ec = 0;
//             }
//             if (ec > 0) throw std::system_error{ec, std::system_category()};
//
//             if (cqe) {
//                 if (auto user_data = ::io_uring_cqe_get_data(cqe); user_data and user_data != this) {
//                     auto op = static_cast<uring_op_base*>(user_data);
//                     op->complete(op, cqe->res);
//                     local_op_queue.unsynchronized_enqueue(*op);
//                 }
//             }
//             cqe_guard.reset();
//
//             while (true) {
//                 ::io_uring_cqe* peeked_cqes[8]{};
//                 const auto n = ::io_uring_peek_batch_cqe(&uring_, peeked_cqes, std::ranges::size(peeked_cqes));
//                 if (n == 0) break;
//                 scope_exit _{[this, n]() noexcept {
//                     ::io_uring_cq_advance(&uring_, n);
//                 }};
//                 for (auto peeked_cqe : std::span(peeked_cqes, n)) {
//                     if (auto user_data = ::io_uring_cqe_get_data(peeked_cqe); user_data and user_data != this) {
//                         auto op = static_cast<uring_op_base*>(user_data);
//                         op->complete(op, peeked_cqe->res);
//                         local_op_queue.unsynchronized_enqueue(*op);
//                     }
//                 }
//             }
//
//             lock.unlock();
//             op_queue_.splice(std::move(local_op_queue));
//
//             if (not infinite) break;
//         }
//         return false;
//     }
//
//     auto uring_context::allocate_sqe() noexcept -> io_uring_sqe* {
//         ::io_uring_sqe* sqe = ::io_uring_get_sqe(&uring_);
//         if (sqe == nullptr) {
//             if (::io_uring_submit(&uring_) < 0) return nullptr;
//             sqe = ::io_uring_get_sqe(&uring_);
//         }
//         return sqe;
//     }
//
//     auto uring_context::interrupt() -> void {
//         std::scoped_lock _{mtx_};
//         auto sqe = allocate_sqe();
//         if (sqe == nullptr) {
//             throw std::system_error{std::make_error_code(std::errc::no_buffer_space)};
//         }
//         ::io_uring_prep_nop(sqe);
//         ::io_uring_sqe_set_data(sqe, this);
//         throw_uring_error(::io_uring_submit(&uring_));
//     }
//
//     namespace detail {
//         native_uring_sexpr<async_send_to_t>::type::type(async_send_to_t s) noexcept {
//             peer = endpoint_to_sockaddr_in(s.peer);
//             auto [psa, len] = to_sockaddr(peer);
//             buffer = {
//                 .iov_base = const_cast<std::byte*>(s.buffer.data()),
//                 .iov_len = s.buffer.size()
//             };
//             msg = {
//                 .msg_name = psa,
//                 .msg_namelen = len,
//                 .msg_iov = &buffer,
//                 .msg_iovlen = 1
//             };
//         }
//
//         native_uring_sexpr<async_receive_from_t>::type::type(async_receive_from_t s) noexcept {
//             peer = endpoint_to_sockaddr_in(s.peer);
//             auto [psa, len] = to_sockaddr(peer);
//             buffer = {
//                 .iov_base = s.buffer.data(),
//                 .iov_len = s.buffer.size()
//             };
//             msg = {
//                 .msg_name = psa,
//                 .msg_namelen = len,
//                 .msg_iov = &buffer,
//                 .msg_iovlen = 1
//             };
//         }
//
//         native_uring_sexpr<async_connect_t>::type::type(async_connect_t s) noexcept : peer(endpoint_to_sockaddr_in(s.peer)) {}
//
//
//         template<>
//         auto uring_op_base_for<async_read_some_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), -1);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//         template<>
//         auto uring_op_base_for<async_write_some_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), -1);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//         template<>
//         auto uring_op_base_for<async_read_some_at_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), offset);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//         template<>
//         auto uring_op_base_for<async_write_some_at_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), offset);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//         template<>
//         auto uring_op_base_for<async_receive_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_recv(sqe, fd, buffer.data(), buffer.size(), 0);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//
//         template<>
//         auto uring_op_base_for<async_send_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_send(sqe, fd, buffer.data(), buffer.size(), MSG_NOSIGNAL);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//         template<>
//         auto uring_op_base_for<async_receive_from_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_recvmsg(sqe, fd, &msg, 0);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//
//         template<>
//         auto uring_op_base_for<async_send_to_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_sendmsg(sqe, fd, &msg, MSG_NOSIGNAL);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//
//         template<>
//         auto uring_op_base_for<async_accept_t>::start() noexcept -> bool {
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_accept(sqe, fd, nullptr, nullptr, 0);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//
//
//         template<>
//         auto uring_op_base_for<async_connect_t>::start() noexcept -> bool {
//             auto [psa, len] = to_sockaddr(peer);
//             auto sqe = context_.allocate_sqe();
//             if (sqe == nullptr) {
//                 result.error(std::make_error_code(std::errc::no_buffer_space));
//                 return false;
//             }
//             ::io_uring_prep_connect(sqe, fd, psa, len);
//             ::io_uring_sqe_set_data(sqe, static_cast<uring_op_base*>(this));
//             if (auto ec = -::io_uring_submit(&context_.uring_); ec > 0) {
//                 result.error(std::error_code{ec, std::system_category()});
//                 return false;
//             }
//             return true;
//         }
//     }
//
// }
// #endif