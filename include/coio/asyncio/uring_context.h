#pragma once
#include "../detail/config.h"
#if not COIO_HAS_IO_URING
#error "uh, where is <liburing.h>?"
#endif
#include <liburing.h>
#include <netinet/in.h>
#include "../execution_context.h"
#include "../detail/io_descriptions.h"
#include "../detail/async_result.h"

namespace coio {
    namespace detail {
        template<typename Sexpr>
        class uring_op;
    }

    class uring_context : public detail::run_loop_base<uring_context> {
        template<typename Sexpr>
        friend class detail::uring_op;
        friend run_loop_base;

    private:
        struct io_op : operation_base {
            using operation_base::operation_base;
            auto (*complete)(io_op* op, int cqe_res) noexcept -> void = nullptr;
        };

    public:
        class scheduler : public scheduler_base {
            friend uring_context;
        public:
            using scheduler_concept = detail::io_scheduler_tag;

            class fd_entry {
                friend scheduler;
            private:
                struct private_data {
                    operation_base* in_op{nullptr};
                    operation_base* out_op{nullptr};

                    auto set_in_op(operation_base* op) -> void {
                        std::atomic_ref{in_op}.store(op, std::memory_order_release);
                    }

                    auto get_in_op() const noexcept -> operation_base* {
                        return std::atomic_ref{in_op}.load(std::memory_order_acquire);
                    }

                    auto exchange(operation_base* op) noexcept -> operation_base* {
                        return std::atomic_ref{out_op}.exchange(op, std::memory_order_acq_rel);
                    }
                };

            public:
                fd_entry(uring_context& ctx, int fd) noexcept : ctx_(&ctx), fd_(fd) {}

                fd_entry(const fd_entry&) = delete;

                fd_entry(fd_entry&& other) noexcept :
                    ctx_(other.ctx_),
                    fd_(std::exchange(other.fd_, -1)) {}

                ~fd_entry() {
                    cancel();
                }

                auto operator= (fd_entry other) noexcept -> fd_entry& {
                    std::ranges::swap(ctx_, other.ctx_);
                    std::ranges::swap(fd_, other.fd_);
                    return *this;
                }

            public:
                [[nodiscard]]
                COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler {
                    COIO_ASSERT(ctx_ != nullptr);
                    return scheduler{*ctx_};
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto native_handle() const noexcept -> int {
                    return fd_;
                }

                auto release() -> int;

                auto cancel() -> void;

            private:
                uring_context* ctx_;
                int fd_ = -1;
            };

            template<std::move_constructible Sexpr>
            struct io_awaitable {
                int fd;
                uring_context* context;
                Sexpr sexpr;

                COIO_ALWAYS_INLINE auto operator co_await() && noexcept -> detail::uring_op<Sexpr> {
                    COIO_ASSERT(fd != -1 and context != nullptr);
                    return {
                        std::exchange(fd, -1),
                        *std::exchange(context, nullptr),
                        std::move(sexpr)
                    };
                }

    #ifdef COIO_ENABLE_SENDERS
                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*context};
                }
    #endif
            };

        public:
            using scheduler_base::scheduler_base;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto wrap_fd(int fd) const -> fd_entry {
                return fd_entry{*ctx_, fd};
            }

            template<typename Sexpr>
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto schedule_io(fd_entry& entry, Sexpr sexpr) noexcept -> io_awaitable<Sexpr> {
                return {entry.fd_, ctx_, std::move(sexpr)};
            }

        };

    public:
        explicit uring_context(std::size_t entries, std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());

        uring_context();

        uring_context(const uring_context&) = delete;

        ~uring_context();

        auto operator= (const uring_context&) -> uring_context& = delete;

    private:
        auto do_one(bool infinite) -> bool;

        auto interrupt() -> void;

        auto allocate_sqe() noexcept -> ::io_uring_sqe*;

    private:
        ::io_uring uring_{};
    };

    namespace detail {
        struct uring_msghdr {
            std::variant<::sockaddr_in, ::sockaddr_in6> peer;
            ::iovec buffer;
            ::msghdr msg;
        };

        template<typename Sexpr>
        struct native_uring_sexpr {
            using type = Sexpr;
        };

        template<>
        struct native_uring_sexpr<async_send_to_t> {
            struct type : uring_msghdr {
                type(async_send_to_t s) noexcept;
            };
        };

        template<>
        struct native_uring_sexpr<async_receive_from_t> {
            struct type : uring_msghdr {
                type(async_receive_from_t s) noexcept;
            };
        };

        template<>
        struct native_uring_sexpr<async_connect_t> {
            struct type {
                type(async_connect_t s) noexcept;
                std::variant<::sockaddr_in, ::sockaddr_in6> peer;
            };
        };

        template<typename Sexpr>
        class uring_op : private native_uring_sexpr<Sexpr>::type, public uring_context::io_op {
        private:
            using base1 = typename native_uring_sexpr<Sexpr>::type;

        public:
            uring_op(int fd, uring_context& context, Sexpr sexpr) noexcept;

            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> bool;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto await_resume() -> typename Sexpr::result_type {
                return result.get(Sexpr::operation_name());
            }

        private:
            // NOLINTBEGIN(*-use-equals-delete)
            auto start() noexcept -> bool = delete;
            // NOLINTEND(*-use-equals-delete)

            auto do_complete(int cqe_res) noexcept -> void {
                if (cqe_res < 0) {
                    result.error(std::error_code{-cqe_res, std::system_category()});
                }
                else {
                    if constexpr (std::is_void_v<typename Sexpr::result_type>) {
                        result.value();
                    }
                    else {
                        result.value(cqe_res);
                    }
                }
            }

        private:
            int fd;
            async_result<typename Sexpr::result_type> result;
        };

        template<>
        auto uring_op<async_read_some_t>::start() noexcept -> bool;

        template<>
        auto uring_op<async_write_some_t>::start() noexcept -> bool;

        template<>
        auto uring_op<async_receive_t>::start() noexcept -> bool;

        template<>
        auto uring_op<async_send_t>::start() noexcept -> bool;

        template<>
        auto uring_op<async_receive_from_t>::start() noexcept -> bool;

        template<>
        auto uring_op<async_send_to_t>::start() noexcept -> bool;

        template<>
        auto uring_op<async_accept_t>::start() noexcept -> bool;

        template<>
        auto uring_op<async_connect_t>::start() noexcept -> bool;

        template <typename Sexpr>
        uring_op<Sexpr>::uring_op(int fd, uring_context& context, Sexpr sexpr) noexcept:
            base1(std::move(sexpr)),
            io_op(context),
            fd(fd)
        {
            this->complete = +[](io_op* self, int cqe_res) noexcept -> void {
                COIO_ASSERT(self != nullptr);
                static_cast<uring_op*>(self)->do_complete(cqe_res);
            };
        }

        template<typename Sexpr>
        template<typename Promise>
        auto uring_op<Sexpr>::await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> bool {
            coro_ = this_coro;
            if constexpr (stoppable_promise<Promise>) {
                unhandled_stopped_ = &stop_stoppable_coroutine_<Promise>;
            }
            return start();
        }
    }
}