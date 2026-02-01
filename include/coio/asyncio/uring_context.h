// ReSharper disable CppRedundantTypenameKeyword
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

namespace coio {
    namespace detail {
        template<typename Sexpr>
        class uring_state_base_for;
    }

    class uring_context : public detail::loop_base<uring_context> {
        template<typename Sexpr>
        friend class detail::uring_state_base_for;
        friend loop_base;

    private:
        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        struct uring_node : node {
            uring_node(uring_context& context) noexcept : node(context) {}

            auto do_cancel() -> void;

            virtual auto complete(int cqe_res) -> void = 0;
        };

    public:
        class scheduler : public scheduler_base {
            friend uring_context;
        public:
            using scheduler_concept = detail::io_scheduler_t;

            class io_object {
                friend scheduler;
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
                struct state_base : detail::uring_state_base_for<Sexpr> {
                    using base = detail::uring_state_base_for<Sexpr>;

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
                    COIO_ASSERT(fd != -1 and context != nullptr);
                    return state<Rcvr>{
                        std::move(rcvr),
                        std::exchange(fd, -1),
                        *std::exchange(context, nullptr),
                        std::move(sexpr)
                    };
                }

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*context};
                }

                int fd;
                uring_context* context;
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
                return stop_when(io_sender<Sexpr>{obj.fd_, ctx_, std::move(sexpr)}, ctx_->stop_source_.get_token());
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
        std::mutex uring_mtx_;
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
        class uring_state_base_for : private native_uring_sexpr<Sexpr>::type, public uring_context::uring_node {
        private:
            using base1 = typename native_uring_sexpr<Sexpr>::type;

        public:
            uring_state_base_for(int fd, uring_context& context, Sexpr sexpr) noexcept :
                base1(std::move(sexpr)),
                uring_node(context),
                fd(fd) {}

        protected:
            auto do_start() noexcept -> bool {
                static_assert(always_false<Sexpr>, "this operation isn't supported");
                unreachable();
            }

            auto complete(int cqe_res) -> void override {
                if (cqe_res < 0) {
                    if (-cqe_res == ECANCELED) {
                        result.set_stopped();
                    }
                    else {
                        result.set_error(std::error_code{-cqe_res, std::system_category()});
                    }
                }
                else {
                    if constexpr (std::is_void_v<typename Sexpr::result_type>) {
                        result.set_value();
                    }
                    else {
                        result.set_value(cqe_res);
                    }
                }
            }

        protected:
            int fd;
            async_result<typename Sexpr::result_type, std::error_code> result;
        };

        /// async_read_some
        template<>
        auto uring_state_base_for<async_read_some_t>::do_start() noexcept -> bool;

        /// async_write_some
        template<>
        auto uring_state_base_for<async_write_some_t>::do_start() noexcept -> bool;

        /// async_read_some_at
        template<>
        auto uring_state_base_for<async_read_some_at_t>::do_start() noexcept -> bool;

        /// async_write_some_at
        template<>
        auto uring_state_base_for<async_write_some_at_t>::do_start() noexcept -> bool;

        /// async_receive
        template<>
        auto uring_state_base_for<async_receive_t>::do_start() noexcept -> bool;

        /// async_send
        template<>
        auto uring_state_base_for<async_send_t>::do_start() noexcept -> bool;

        /// async_receive_from
        template<>
        auto uring_state_base_for<async_receive_from_t>::do_start() noexcept -> bool;

        /// async_send_to
        template<>
        auto uring_state_base_for<async_send_to_t>::do_start() noexcept -> bool;

        /// async_accept
        template<>
        auto uring_state_base_for<async_accept_t>::do_start() noexcept -> bool;

        /// async_connect
        template<>
        auto uring_state_base_for<async_connect_t>::do_start() noexcept -> bool;
    }
}