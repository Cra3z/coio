#pragma once
#include "../detail/config.h"
#if not COIO_HAS_IO_URING
#error "uh, where is <liburing.h>?"
#endif
#include <liburing.h>
#include <netinet/in.h>
#include "../execution_context.h"
#include "../detail/async_result.h"
#include "../detail/io_descriptions.h"
#include "../detail/stoppable_op.h"

namespace coio {
    namespace detail {
        template<typename Sexpr>
        class uring_op_base_for;

        template<typename Sexpr, typename StopToken>
        class uring_op;
    }

    class uring_context : public detail::run_loop_base<uring_context> {
        template<typename Sexpr>
        friend class detail::uring_op_base_for;
        friend run_loop_base;

    private:
        struct uring_op_base : operation_base {
            using complete_fn_t = auto (*)(uring_op_base* op, int cqe_res) -> void;

            uring_op_base(uring_context& context, complete_fn_t complete) noexcept : operation_base(context), complete(complete) {}

            auto cancel() -> void;

            const complete_fn_t complete = nullptr;
        };

    public:
        class scheduler : public scheduler_base {
            friend uring_context;
        public:
            using scheduler_concept = detail::io_scheduler_tag;

            class io_object {
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
                io_object(uring_context& ctx, int fd) noexcept : ctx_(&ctx), fd_(fd) {}

                io_object(const io_object&) = delete;

                io_object(io_object&& other) noexcept :
                    ctx_(other.ctx_),
                    fd_(std::exchange(other.fd_, -1)) {}

                ~io_object() {
                    cancel();
                }

                auto operator= (io_object other) noexcept -> io_object& {
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

            template<std::move_constructible Sexpr, stoppable_token StopToken>
            struct io_awaitable {
                int fd;
                uring_context* context;
                Sexpr sexpr;
                StopToken stop_token;

                COIO_ALWAYS_INLINE auto operator co_await() && noexcept -> detail::uring_op<Sexpr, StopToken> {
                    COIO_ASSERT(fd != -1 and context != nullptr);
                    return {
                        std::move(stop_token),
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
            COIO_ALWAYS_INLINE auto make_io_object(int fd) const -> io_object {
                return io_object{*ctx_, fd};
            }

            template<typename Sexpr, stoppable_token StopToken>
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto schedule_io(io_object& obj, Sexpr sexpr, StopToken stop_token) noexcept -> io_awaitable<Sexpr, StopToken> {
                return {obj.fd_, ctx_, std::move(sexpr), std::move(stop_token)};
            }
        };

    public:
        explicit uring_context(std::size_t entries, std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());

        uring_context();

        uring_context(const uring_context&) = delete;

        ~uring_context();

        auto operator= (const uring_context&) -> uring_context& = delete;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_uring() noexcept -> ::io_uring* {
            return &uring_;
        }

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
        class uring_op_base_for : private native_uring_sexpr<Sexpr>::type, public uring_context::uring_op_base {
        private:
            using base1 = typename native_uring_sexpr<Sexpr>::type;

        public:
            uring_op_base_for(int fd, uring_context& context, Sexpr sexpr) noexcept :
                base1(std::move(sexpr)),
                uring_op_base(context, &do_complete_thunk),
                fd(fd) {}

            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename Promise>
            auto await_suspend_impl(std::coroutine_handle<Promise> this_coro) noexcept -> bool;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto await_resume_impl() -> typename Sexpr::result_type {
                return result.get(Sexpr::operation_name());
            }

        private:
            // NOLINTBEGIN(*-use-equals-delete)
            auto start() noexcept -> bool = delete;
            // NOLINTEND(*-use-equals-delete)

            auto do_complete(int cqe_res) -> void {
                if (cqe_res < 0) {
                    if (-cqe_res == ECANCELED) {
                        unhandled_stopped_(coro_).resume();
                    }
                    else {
                        result.error(std::error_code{-cqe_res, std::system_category()});
                    }
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

            static auto do_complete_thunk(uring_op_base* self, int cqe_res) -> void;

        private:
            int fd;
            async_result<typename Sexpr::result_type> result;
        };

        template<>
        auto uring_op_base_for<async_read_some_t>::start() noexcept -> bool;

        template<>
        auto uring_op_base_for<async_write_some_t>::start() noexcept -> bool;

        template<>
        auto uring_op_base_for<async_receive_t>::start() noexcept -> bool;

        template<>
        auto uring_op_base_for<async_send_t>::start() noexcept -> bool;

        template<>
        auto uring_op_base_for<async_receive_from_t>::start() noexcept -> bool;

        template<>
        auto uring_op_base_for<async_send_to_t>::start() noexcept -> bool;

        template<>
        auto uring_op_base_for<async_accept_t>::start() noexcept -> bool;

        template<>
        auto uring_op_base_for<async_connect_t>::start() noexcept -> bool;

        template<typename Sexpr>
        auto uring_op_base_for<Sexpr>::do_complete_thunk(uring_op_base* self, int cqe_res) -> void {
            COIO_ASSERT(self != nullptr);
            static_cast<uring_op_base_for*>(self)->do_complete(cqe_res);
        }

        template<typename Sexpr>
        template<typename Promise>
        auto uring_op_base_for<Sexpr>::await_suspend_impl(std::coroutine_handle<Promise> this_coro) noexcept -> bool {
            coro_ = this_coro;
            if constexpr (stoppable_promise<Promise>) {
                unhandled_stopped_ = &stop_stoppable_coroutine_<Promise>;
            }
            return start();
        }

        template<typename Sexpr, typename StopToken>
        class uring_op : public stoppable_op<uring_op_base_for<Sexpr>, StopToken> {
        private:
            using base = stoppable_op<uring_op_base_for<Sexpr>, StopToken>;
        public:
            using base::base;
        };
    }
}