// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
#pragma once
#include "../detail/config.h"
#if not COIO_HAS_EPOLL
#error "uh, where is <sys/epoll.h>?"
#endif
#include "../execution_context.h"
#include "../detail/async_result.h"
#include "../detail/io_descriptions.h"

namespace coio {
    namespace detail {
        template<typename Sexpr>
        class epoll_op_base_for;

        template<typename Sexpr, typename Rcvr>
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

    class epoll_context : public detail::loop_base<epoll_context> {
        template<typename Sexpr>
        friend class detail::epoll_op_base_for;
        friend loop_base;
    private:
        class epoll_op_base;

        struct epoll_data {
            std::atomic<bool> registered{false};
            std::atomic<epoll_op_base*> in_op{nullptr};
            std::atomic<epoll_op_base*> out_op{nullptr};
        };

        class epoll_op_base : public operation_base {
            friend epoll_context;
        public:
            epoll_op_base(epoll_context& context, int fd, epoll_data* data) noexcept : operation_base(context), fd(fd), data(data) {}

        protected:
            [[nodiscard]]
            auto register_event(int event_type, uint32_t extra_flags) noexcept -> bool;

            COIO_ALWAYS_INLINE auto immediate_complete() -> void {
                context_.op_queue_.enqueue(*this);
                context_.interrupt();
            }
        private:
            virtual auto perform() noexcept -> void = 0;

        protected:
            int fd;
            epoll_data* data;
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

            template<std::move_constructible Sexpr>
            struct io_sender {
                using sender_concept = execution::sender_t;
                using completion_signatures = execution::completion_signatures<
                    execution::set_value_t(typename Sexpr::result_type),
                    execution::set_error_t(std::exception_ptr),
                    execution::set_stopped_t()
                >;

                template<execution::receiver_of<completion_signatures> Rcvr>
                COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept -> detail::epoll_op<Sexpr, Rcvr> {
                    COIO_ASSERT(fd != -1 and context != nullptr and data != nullptr);
                    return {
                        std::move(rcvr),
                        std::exchange(fd, -1),
                        *std::exchange(context, nullptr),
                        std::exchange(data, nullptr),
                        std::move(sexpr)
                    };
                }

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*context};
                }

                int fd;
                epoll_context* context;
                epoll_data* data;
                Sexpr sexpr;
            };

        public:
            using scheduler_base::scheduler_base;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto make_io_object(int fd) const -> io_object {
                return io_object{*ctx_, fd};
            }

            template<typename Sexpr>
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto schedule_io(io_object& obj, Sexpr sexpr) noexcept -> io_sender<Sexpr> {
                return {obj.fd_, ctx_, obj.data_, std::move(sexpr)};
            }

        public:
            friend auto operator== (const scheduler& lhs, const scheduler& rhs) -> bool = default;
        };

    private:
        explicit epoll_context(std::nullptr_t, std::pmr::memory_resource& memory_resource) noexcept :
            loop_base(memory_resource), epoll_fd_(-1) {}

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
                epoll_op_base(context, fd, data) {}

        protected:
            // NOLINTBEGIN(*-use-equals-delete)
            auto do_cancel() -> void = delete;

            auto do_start() noexcept -> bool = delete;

            auto do_perform() noexcept -> void = delete;
            // NOLINTEND(*-use-equals-delete)

            auto perform() noexcept -> void override;

        private:
            async_result<typename Sexpr::result_type, std::error_code> result;
        };

        template<>
        auto epoll_op_base_for<async_read_some_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_read_some_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_read_some_t>::do_cancel() -> void;


        template<>
        auto epoll_op_base_for<async_write_some_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_write_some_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_write_some_t>::do_cancel() -> void;


        template<>
        auto epoll_op_base_for<async_send_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_send_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_send_t>::do_cancel() -> void;


        template<>
        auto epoll_op_base_for<async_receive_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_receive_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_receive_t>::do_cancel() -> void;


        template<>
        auto epoll_op_base_for<async_receive_from_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_receive_from_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_receive_from_t>::do_cancel() -> void;


        template<>
        auto epoll_op_base_for<async_send_to_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_send_to_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_send_to_t>::do_cancel() -> void;


        template<>
        auto epoll_op_base_for<async_accept_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_accept_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_accept_t>::do_cancel() -> void;


        template<>
        auto epoll_op_base_for<async_connect_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_op_base_for<async_connect_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_op_base_for<async_connect_t>::do_cancel() -> void;

        template<typename Sexpr>
        auto epoll_op_base_for<Sexpr>::perform() noexcept -> void {
            this->do_perform();
        }

        template<typename Sexpr, typename Rcvr>
        class epoll_op : public epoll_context::cancellable<Rcvr, epoll_op_base_for<Sexpr>> {
        private:
            using base = epoll_context::cancellable<Rcvr, epoll_op_base_for<Sexpr>>;
        public:
            using base::base;

            auto start_impl() noexcept -> void override {
                if (not this->do_start()) {
                    this->finish();
                }
            }

            auto finish_impl() noexcept -> void override {
                this->result.forward_to(std::move(this->rcvr_));
            }

            auto cancel() -> void override {
                this->do_cancel();
            }
        };
    }
}