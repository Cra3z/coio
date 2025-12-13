#pragma once
#include "../detail/config.h"
#if not COIO_HAS_EPOLL
#error "uh, where is <sys/epoll.h>?"
#endif
#include "../execution_context.h"
#include "../detail/io_descriptions.h"
#include "../detail/async_result.h"

namespace coio {
    namespace detail {
        template<typename Sexpr>
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
        friend class detail::epoll_op;
        friend run_loop_base;
    private:
        struct epoll_data {
            std::atomic<bool> registered{false};
            std::atomic<operation_base*> in_op{nullptr};
            std::atomic<operation_base*> out_op{nullptr};
        };
    public:
        class scheduler : public scheduler_base {
            friend epoll_context;
        public:
            using scheduler_concept = detail::io_scheduler_tag;

            class fd_entry {
                friend scheduler;
            public:
                fd_entry(epoll_context& ctx, int fd);

                fd_entry(const fd_entry&) = delete;

                fd_entry(fd_entry&& other) noexcept : ctx_(other.ctx_), fd_(std::exchange(other.fd_, -1)), data_(std::move(other.data_)) {}

                ~fd_entry() = default;

                auto operator= (fd_entry other) noexcept -> fd_entry& {
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
                std::unique_ptr<epoll_data> data_;
            };

            template<std::move_constructible Sexpr>
            struct io_awaitable {
                int fd;
                epoll_context* context;
                epoll_data* data;
                Sexpr sexpr;

                COIO_ALWAYS_INLINE auto operator co_await() && noexcept -> detail::epoll_op<Sexpr> {
                    COIO_ASSERT(fd != -1 and context != nullptr and data != nullptr);
                    return {
                        std::exchange(fd, -1),
                        *std::exchange(context, nullptr),
                        std::exchange(data, nullptr),
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
                return {entry.fd_, ctx_, entry.data_.get(), std::move(sexpr)};
            }

        public:
            friend auto operator== (const scheduler& lhs, const scheduler& rhs) -> bool = default;
        };

    private:
        explicit epoll_context(std::nullptr_t) noexcept :
            epoll_fd_(-1) {}

        explicit epoll_context(std::nullptr_t, std::pmr::memory_resource& memory_resource) noexcept :
            run_loop_base(memory_resource), epoll_fd_(-1) {}

    public:
        epoll_context();

        explicit epoll_context(std::pmr::memory_resource& memory_resource) : epoll_context(nullptr, memory_resource) {}

        ~epoll_context();

        auto request_stop() -> bool;

    private:
        auto do_one(bool infinite) -> bool;

        COIO_ALWAYS_INLINE auto interrupt() -> void {
            interrupter_.interrupt();
        }

    private:
        int epoll_fd_;
        detail::reactor_interrupter interrupter_;
    };

    namespace detail {
        template<typename Sexpr>
        class epoll_op : private Sexpr, public epoll_context::operation_base {
        public:
            epoll_op(int fd, epoll_context& context, epoll_context::epoll_data* data, Sexpr sexpr) noexcept :
                Sexpr(std::move(sexpr)),
                operation_base(context),
                fd(fd),
                data(data) {}

            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> void;

            [[nodiscard]]
            auto await_resume() -> typename Sexpr::result_type;

        private:
            // NOLINTBEGIN(*-use-equals-delete)
            auto start() noexcept -> void = delete;

            auto on_completion() -> typename Sexpr::result_type = delete;
            // NOLINTEND(*-use-equals-delete)

            COIO_ALWAYS_INLINE auto immediate_complete() -> void {
                context_.op_queue_.enqueue(*this);
                context_.interrupt();
            }

            auto register_(int event_type, uint32_t extra_flags) noexcept -> void;

        private:
            int fd;
            epoll_context::epoll_data* data;
            detail::async_result<typename Sexpr::result_type> result;
        };

        template<>
        auto epoll_op<async_send_t>::start() noexcept -> void;

        template<>
        auto epoll_op<async_send_t>::on_completion() -> result_type;


        template<>
        auto epoll_op<async_receive_t>::start() noexcept -> void;

        template<>
        auto epoll_op<async_receive_t>::on_completion() -> result_type;


        template<>
        auto epoll_op<async_receive_from_t>::start() noexcept -> void;

        template<>
        auto epoll_op<async_receive_from_t>::on_completion() -> result_type;


        template<>
        auto epoll_op<async_send_to_t>::start() noexcept -> void;

        template<>
        auto epoll_op<async_send_to_t>::on_completion() -> result_type;


        template<>
        auto epoll_op<async_accept_t>::start() noexcept -> void;

        template<>
        auto epoll_op<async_accept_t>::on_completion() -> result_type;


        template<>
        auto epoll_op<async_connect_t>::start() noexcept -> void;

        template<>
        auto epoll_op<async_connect_t>::on_completion() -> result_type;

        template<typename Sexpr>
        template<typename Promise>
        auto epoll_op<Sexpr>::await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> void {
            coro_ = this_coro;
            if constexpr (stoppable_promise<Promise>) {
                unhandled_stopped_ = &stop_stoppable_coroutine_<Promise>;
            }
            start();
        }

        template<typename Sexpr>
        auto epoll_op<Sexpr>::await_resume() -> typename Sexpr::result_type {
            return on_completion();
        }
    }
}