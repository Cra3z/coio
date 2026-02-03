// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
#pragma once
#include "../detail/config.h"
#if not COIO_HAS_EPOLL
#error "uh, where is <sys/epoll.h>?"
#endif
#include "../execution_context.h"
#include "../detail/async_result.h"
#include "../detail/io_descriptions.h"
#include "../utils/atomutex.h"

namespace coio {
    namespace detail {
        template<typename Sexpr>
        class epoll_state_base_for;

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
        friend class detail::epoll_state_base_for;
        friend loop_base;
    private:
        class epoll_node;

        struct epoll_data {
            atomutex fd_lock;
            std::uint32_t events{};
            epoll_node* in_op{nullptr};
            epoll_node* out_op{nullptr};
        };

        class epoll_node : public node {
            friend epoll_context;
        public:
            epoll_node(epoll_context& context, int fd, epoll_data* data) noexcept : node(context), fd(fd), data(data) {}

        protected:
            [[nodiscard]]
            auto register_event(int event_type, std::uint32_t extra_flags) noexcept -> bool;

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
                    detail::set_value_t<typename Sexpr::result_type>,
                    execution::set_error_t(std::error_code),
                    execution::set_error_t(std::exception_ptr),
                    execution::set_stopped_t()
                >;

                template<typename Rcvr>
                struct state_base : detail::epoll_state_base_for<Sexpr> {
                    using base = detail::epoll_state_base_for<Sexpr>;

                    template<typename... Args>
                    state_base(Rcvr rcvr, Args&&... args) noexcept : base(std::forward<Args>(args)...), rcvr_(std::move(rcvr)) {}

                    COIO_ALWAYS_INLINE auto do_finish() noexcept -> void {
                        this->result.forward_to(std::move(this->rcvr_));
                    }

                    Rcvr rcvr_;
                };

                template<typename Rcvr>
                using state = operation_state<state_base<Rcvr>>;

                template<execution::receiver_of<completion_signatures> Rcvr>
                COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                    COIO_ASSERT(fd != -1 and context != nullptr and data != nullptr);
                    return state<Rcvr>{
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
            COIO_ALWAYS_INLINE auto schedule_io(io_object& obj, Sexpr sexpr) noexcept {
                return stop_when(io_sender<Sexpr>{obj.fd_, ctx_, obj.data_, std::move(sexpr)}, ctx_->stop_source_.get_token());
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

        auto cancel_op(int event, epoll_node* op) -> void;

    private:
        int epoll_fd_;
        detail::reactor_interrupter interrupter_;
        std::mutex run_mtx_;
    };

    namespace detail {
        template<typename Sexpr>
        class epoll_state_base_for : private Sexpr, public epoll_context::epoll_node {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::epoll_data* data, Sexpr sexpr) noexcept :
                Sexpr(std::move(sexpr)),
                epoll_node(context, fd, data) {}

        protected:
            auto do_cancel() -> void {
                static_assert(always_false<Sexpr>, "this operation isn't supported");
            }

            auto do_start() noexcept -> bool {
                static_assert(always_false<Sexpr>, "this operation isn't supported");
                unreachable();
            }

            auto do_perform() noexcept -> void {
                static_assert(always_false<Sexpr>, "this operation isn't supported");
            }

            auto perform() noexcept -> void override {
                do_perform();
            }

        protected:
            async_result<typename Sexpr::result_type, std::error_code> result;
        };

        /// async_read_some
        template<>
        auto epoll_state_base_for<async_read_some_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_read_some_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_read_some_t>::do_cancel() -> void;


        /// async_write_some
        template<>
        auto epoll_state_base_for<async_write_some_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_write_some_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_write_some_t>::do_cancel() -> void;


        /// async_send
        template<>
        auto epoll_state_base_for<async_send_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_send_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_send_t>::do_cancel() -> void;


        /// async_receive
        template<>
        auto epoll_state_base_for<async_receive_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_receive_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_receive_t>::do_cancel() -> void;


        /// async_receive_from
        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_receive_from_t>::do_cancel() -> void;


        /// async_send_to
        template<>
        auto epoll_state_base_for<async_send_to_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_send_to_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_send_to_t>::do_cancel() -> void;


        /// async_accept
        template<>
        auto epoll_state_base_for<async_accept_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_accept_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_accept_t>::do_cancel() -> void;


        /// async_connect
        template<>
        auto epoll_state_base_for<async_connect_t>::do_start() noexcept -> bool;

        template<>
        auto epoll_state_base_for<async_connect_t>::do_perform() noexcept -> void;

        template<>
        auto epoll_state_base_for<async_connect_t>::do_cancel() -> void;
    }
}