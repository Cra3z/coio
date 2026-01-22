#pragma once
#include "../detail/config.h"
#if not COIO_HAS_EPOLL
#error "uh, where is <sys/epoll.h>?"
#endif
#include "../execution_context.h"
#include "../detail/async_result.h"
#include "../detail/io_descriptions.h"
#include "../detail/stoppable_op.h"

namespace coio {
    namespace detail {
        template<typename Sexpr>
        class epoll_op_base_for;

        template<typename Sexpr, typename StopToken>
        class epoll_op;

        class reactor_interrupter {
        public:
            reactor_interrupter();

            reactor_interrupter(const reactor_interrupter&) = delete;

            ~reactor_interrupter();

            auto operator= (const reactor_interrupter&) -> reactor_interrupter& = delete;

            auto interrupt() -> void;

            auto reset() -> bool;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto watcher() const noexcept -> int {
                return reader_;
            }

        private:
            int reader_;
            int writer_;
        };
    }

    class epoll_context : public detail::run_loop_base<epoll_context> {
        template<typename Sexpr>
        friend class detail::epoll_op_base_for;
        friend run_loop_base;
    private:
        class epoll_op_base;

        struct epoll_data {
            explicit epoll_data(inplace_stop_token token) : on_stopped(std::move(token), std::bind_front(&epoll_data::cancel_ops, this)) {}

            auto cancel_ops() -> void;

            std::atomic<bool> registered{false};
            std::atomic<epoll_op_base*> in_op{nullptr};
            std::atomic<epoll_op_base*> out_op{nullptr};
            stop_callback_for_t<inplace_stop_token, decltype(std::bind_front(&epoll_data::cancel_ops, std::declval<epoll_data*>()))> on_stopped;
        };

        class epoll_op_base : public operation_base {
            friend epoll_context;
        private:
            using perform_fn_t = auto (*)(epoll_op_base*) noexcept -> void;

        public:
            epoll_op_base(epoll_context& context, int fd, epoll_data* data, perform_fn_t perform) noexcept :
                operation_base(context), fd(fd), data(data), perform(perform) {}

        protected:
            [[nodiscard]]
            auto register_event(int event_type, uint32_t extra_flags) noexcept -> bool;

            COIO_ALWAYS_INLINE auto immediate_complete() -> void {
                context_.op_queue_.enqueue(*this);
                context_.interrupt();
            }

        protected:
            int fd;
            epoll_data* data;
            const perform_fn_t perform = nullptr;
        };

    public:
        class scheduler : public scheduler_base {
            friend epoll_context;
        public:
            using scheduler_concept = detail::io_scheduler_t;

            class io_object {
                friend scheduler;
            public:
                io_object(epoll_context& ctx, int fd);

                io_object(const io_object&) = delete;

                io_object(io_object&& other) noexcept :
                    ctx_(other.ctx_),
                    fd_(std::exchange(other.fd_, -1)),
                    data_(std::exchange(other.data_, {})) {}

                ~io_object();

                auto operator= (io_object other) noexcept -> io_object& {
                    std::ranges::swap(ctx_, other.ctx_);
                    std::ranges::swap(fd_, other.fd_);
                    std::ranges::swap(data_, other.data_);
                    return *this;
                }

            public:
                [[nodiscard]]
                COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler {
                    return scheduler{ctx_.get()};
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto native_handle() const noexcept -> int {
                    return fd_;
                }

                auto release() -> int;

                auto cancel() -> void;

            private:
                std::reference_wrapper<epoll_context> ctx_;
                int fd_ = -1;
                epoll_data* data_;
            };

            template<std::move_constructible Sexpr, stoppable_token StopToken>
            struct io_awaitable {
                int fd;
                epoll_context* context;
                epoll_data* data;
                Sexpr sexpr;
                StopToken stop_token;

                COIO_ALWAYS_INLINE auto operator co_await() && noexcept -> detail::epoll_op<Sexpr, StopToken> {
                    COIO_ASSERT(fd != -1 and context != nullptr and data != nullptr);
                    return {
                        std::move(stop_token),
                        std::exchange(fd, -1),
                        *std::exchange(context, nullptr),
                        std::exchange(data, nullptr),
                        std::move(sexpr)
                    };
                }

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*context};
                }
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
                return {obj.fd_, ctx_, obj.data_, std::move(sexpr), std::move(stop_token)};
            }

        public:
            friend auto operator== (const scheduler& lhs, const scheduler& rhs) -> bool = default;
        };

    private:
        explicit epoll_context(std::nullptr_t, std::pmr::memory_resource& memory_resource) noexcept :
            run_loop_base(memory_resource), epoll_fd_(-1) {}

    public:
        explicit epoll_context(std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());

        ~epoll_context();

    private:
        auto do_one(bool infinite) -> bool;

        COIO_ALWAYS_INLINE auto interrupt() -> void {
            interrupter_.interrupt();
        }

        [[nodiscard]]
        auto new_epoll_data() -> epoll_data*;

        auto reclaim_epoll_data(epoll_data* data) noexcept -> void;

        auto cancel_op(int event, epoll_op_base* op) -> void;

    private:
        int epoll_fd_;
        detail::reactor_interrupter interrupter_;
        std::mutex epoll_mtx_;
    };

    namespace detail {
        template<typename Sexpr>
        class epoll_op_base_for : private Sexpr, public epoll_context::epoll_op_base {
        public:
            epoll_op_base_for(int fd, epoll_context& context, epoll_context::epoll_data* data, Sexpr sexpr) noexcept :
                Sexpr(std::move(sexpr)),
                epoll_op_base(context, fd, data, &do_perform_thunk) {}

            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename Promise>
            auto await_suspend_impl(std::coroutine_handle<Promise> this_coro) noexcept -> bool;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto await_resume_impl() -> typename Sexpr::result_type {
                return result.get(Sexpr::operation_name());
            }

            // NOLINTBEGIN(*-use-equals-delete)
            auto cancel() -> void = delete;

        private:
            auto start() noexcept -> bool = delete;

            auto do_perform() noexcept -> void = delete;
            // NOLINTEND(*-use-equals-delete)

            static auto do_perform_thunk(epoll_op_base* self) noexcept -> void;

        private:
            async_result<typename Sexpr::result_type> result;
        };

        template<>
        auto epoll_op_base_for<async_read_some_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_read_some_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_read_some_t>::cancel() -> void;


        template<>
        auto epoll_op_base_for<async_write_some_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_write_some_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_write_some_t>::cancel() -> void;


        template<>
        auto epoll_op_base_for<async_send_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_send_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_send_t>::cancel() -> void;


        template<>
        auto epoll_op_base_for<async_receive_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_receive_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_receive_t>::cancel() -> void;


        template<>
        auto epoll_op_base_for<async_receive_from_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_receive_from_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_receive_from_t>::cancel() -> void;


        template<>
        auto epoll_op_base_for<async_send_to_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_send_to_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_send_to_t>::cancel() -> void;


        template<>
        auto epoll_op_base_for<async_accept_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_accept_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_accept_t>::cancel() -> void;


        template<>
        auto epoll_op_base_for<async_connect_t>::start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_connect_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_connect_t>::cancel() -> void;

        template<typename Sexpr>
        auto epoll_op_base_for<Sexpr>::do_perform_thunk(epoll_op_base* self) noexcept -> void {
            COIO_ASSERT(self != nullptr);
            static_cast<epoll_op_base_for*>(self)->do_perform();
        }

        template<typename Sexpr>
        template<typename Promise>
        auto epoll_op_base_for<Sexpr>::await_suspend_impl(std::coroutine_handle<Promise> this_coro) noexcept -> bool {
            coro_ = this_coro;
            if constexpr (stoppable_promise<Promise>) {
                unhandled_stopped_ = &stop_coroutine<Promise>;
            }
            return start();
        }


        template<typename Sexpr, typename StopToken>
        class epoll_op : public stoppable_op<epoll_op_base_for<Sexpr>, StopToken> {
        private:
            using base = stoppable_op<epoll_op_base_for<Sexpr>, StopToken>;
        public:
            using base::base;
        };
    }
}