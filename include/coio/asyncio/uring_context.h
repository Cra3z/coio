// ReSharper disable CppRedundantTypenameKeyword
// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
#pragma once
#include <coio/detail/config.h>
#if not COIO_HAS_IO_URING
#error "uh, where is <liburing.h>?"
#endif
#include <variant>
#include <liburing.h>
#include <netinet/in.h>
#include <coio/execution_context.h>
#include <coio/utils/async_result.h>
#include <coio/detail/io_descriptions.h>

namespace coio {
    namespace detail {
        template<typename Tag>
        class uring_node_for;

        template<typename Tag>
        class uring_state_base_for;

        enum class seek_whence;
    }

    class uring_context : public detail::loop_base<uring_context> {
        template<typename Tag>
        friend class detail::uring_node_for;
        template<typename Tag>
        friend class detail::uring_state_base_for;
        friend loop_base;

    private:
        struct uring_node : node {
            friend uring_context;
        public:
            uring_node(uring_context& context, int fd) noexcept : node(context), fd(fd) {}

        private:
            virtual auto complete(int cqe_res) noexcept -> void = 0;

        protected:
            auto do_cancel() -> void;

            int fd;
        };

    public:
        class scheduler : public scheduler_base {
            friend uring_context;
        public:
            using scheduler_concept = detail::io_scheduler_tag;

            class io_object {
            public:
                io_object(uring_context& ctx, int fd) noexcept : ctx_(&ctx), fd_(fd) {}

                io_object(const io_object&) = delete;

                io_object(io_object&& other) noexcept :
                    ctx_(other.ctx_),
                    fd_(std::exchange(other.fd_, -1)) {}

                ~io_object();

                auto operator= (io_object other) noexcept -> io_object& {
                    swap(other);
                    return *this;
                }

                auto swap(io_object& other) noexcept -> void {
                    std::ranges::swap(ctx_, other.ctx_);
                    std::ranges::swap(fd_, other.fd_);
                }

                friend auto swap(io_object& lhs, io_object& rhs) noexcept -> void {
                    lhs.swap(rhs);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler {
                    COIO_ASSERT(ctx_ != nullptr);
                    return scheduler{*ctx_};
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto native_handle() const noexcept -> int {
                    return fd_;
                }

                auto close() -> void;

                auto cancel() -> void;

                [[nodiscard]]
                auto receive(std::span<std::byte> buffer) -> std::size_t;

                [[nodiscard]]
                auto send(std::span<const std::byte> buffer) -> std::size_t;

                [[nodiscard]]
                auto receive_from(std::span<std::byte> buffer) -> std::pair<endpoint, std::size_t>;

                [[nodiscard]]
                auto send_to(std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t;

                auto read_some(std::span<std::byte> buffer) -> std::size_t;

                auto write_some(std::span<const std::byte> buffer) -> std::size_t;

                auto read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t;

                auto write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t;

                auto seek(std::size_t offset, detail::seek_whence whence) -> std::size_t;

                auto resize(std::size_t new_size) -> void;

            private:
                template<typename Tag, typename... Args>
                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_initiate(Args... args) noexcept {
                    COIO_ASSERT(ctx_ != nullptr);
                    return stop_when(
                        io_sender<Tag, Args...>{fd_, ctx_, {std::move(args)...}},
                        ctx_->stop_source_.get_token()
                    );
                }

            public:
                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_receive(std::span<std::byte> buffer) noexcept {
                    return async_initiate<detail::receive_tag>(buffer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_send(std::span<const std::byte> buffer) noexcept {
                    return async_initiate<detail::send_tag>(buffer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_receive_from(std::span<std::byte> buffer) noexcept {
                    return async_initiate<detail::receive_from_tag>(buffer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_send_to(std::span<const std::byte> buffer, const endpoint& dest) noexcept {
                    return async_initiate<detail::send_to_tag>(buffer, dest);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_accept() noexcept {
                    return async_initiate<detail::accept_tag>();
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_connect(const endpoint& peer) noexcept {
                    return async_initiate<detail::connect_tag>(peer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_read_some(std::span<std::byte> buffer) noexcept {
                    return async_initiate<detail::read_some_tag>(buffer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_write_some(std::span<const std::byte> buffer) noexcept {
                    return async_initiate<detail::write_some_tag>(buffer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_read_some_at(std::size_t offset, std::span<std::byte> buffer) noexcept {
                    return async_initiate<detail::read_some_at_tag>(offset, buffer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_write_some_at(std::size_t offset, std::span<const std::byte> buffer) noexcept {
                    return async_initiate<detail::write_some_at_tag>(offset, buffer);
                }

            private:
                uring_context* ctx_;
                int fd_ = -1;
            };

            template<typename Tag, typename... Args>
            struct io_sender {
                using sender_concept = execution::sender_tag;
                using completion_signatures = execution::completion_signatures<
                    typename Tag::value_signature,
                    execution::set_error_t(std::error_code),
                    execution::set_stopped_t()
                >;

                template<typename Rcvr>
                struct state_base : detail::uring_state_base_for<Tag> {
                    using base = detail::uring_state_base_for<Tag>;

                    state_base(Rcvr rcvr, int fd, uring_context& context, Args... args) noexcept :
                        base(fd, context, std::move(args)...), rcvr_(std::move(rcvr)) {}

                    COIO_ALWAYS_INLINE auto do_finish() noexcept -> void {
                        this->result.forward_to(std::move(this->rcvr_));
                    }

                    Rcvr rcvr_;
                };

                template<typename Rcvr>
                using state = operation_state<state_base<Rcvr>>;

                template<execution::receiver Rcvr>
                COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                    COIO_ASSERT(context != nullptr);
                    return std::apply(
                        [&](Args&&... args_) {
                            return state<Rcvr>{
                                std::move(rcvr),
                                std::exchange(fd, -1),
                                *std::exchange(context, nullptr),
                                std::move(args_)...
                            };
                        },
                        std::move(args)
                    );
                }

                template<similar_to<io_sender>, typename...>
                static consteval auto get_completion_signatures() noexcept -> completion_signatures {
                    return {};
                }

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*context};
                }

                int fd;
                uring_context* context;
                std::tuple<Args...> args;
            };

        public:
            using scheduler_base::scheduler_base;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto make_io_object(int fd) const -> io_object {
                return io_object{*ctx_, fd};
            }
        };

        template<typename T = void, typename Alloc = void>
        using task = coio::task<T, Alloc, scheduler>;

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

        auto submit_sqes() noexcept -> void;

        auto post_submit_sqes() noexcept -> void;

    private:
        atomutex uring_mtx_;
        std::atomic<bool> pulling_cqes_{false};
        std::size_t pending_sqes_ = 0;
        ::io_uring uring_{};
    };

    namespace detail {
        template<typename Tag>
        class uring_node_for : public uring_context::uring_node {
        public:
            uring_node_for(int fd, uring_context& context) noexcept : uring_node(context, fd) {}

            auto do_start() noexcept -> start_result {
                std::scoped_lock _{context_.uring_mtx_};
                auto sqe = context_.allocate_sqe();
                if (sqe == nullptr) {
                    result.set_error(std::make_error_code(std::errc::no_buffer_space));
                    return start_result::completed;
                }
                static_cast<uring_state_base_for<Tag>*>(this)->prepare(sqe);
                ::io_uring_sqe_set_data(sqe, static_cast<uring_node*>(this));
                // TODO: To suppress TSAN false positives, we need to add more TSAN annotations! see https://github.com/axboe/liburing/issues/1514
                COIO_TSAN_RELEASE(static_cast<uring_node*>(this));
                context_.post_submit_sqes();
                return start_result::pending;
            }

        private:
            auto complete(int cqe_res) noexcept -> void override {
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
                    if constexpr (std::same_as<typename Tag::value_signature, execution::set_value_t()>) {
                        result.set_value();
                    }
                    else if constexpr (requires { result.set_value(cqe_res); }) {
                        result.set_value(cqe_res);
                    }
                    else {
                        unreachable();
                    }
                }
            }

        protected:
            async_result<typename Tag::value_signature, execution::set_error_t(std::error_code)> result;
        };

        template<typename Tag>
        class uring_state_base_for : public uring_node_for<Tag> {
        public:
            template<typename... Args>
            uring_state_base_for(int fd, uring_context& context, const Args&...) noexcept : uring_node_for<Tag>(fd, context) {}

            auto prepare(::io_uring_sqe*) noexcept -> void {
                static_assert(always_false<Tag>, "this operation isn't supported");
            }
        };

        /// async_read_some
        template<>
        class uring_state_base_for<read_some_tag> : public uring_node_for<read_some_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::span<std::byte> buffer) noexcept :
                uring_node_for(fd, context),
                buffer_(buffer) {}

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            std::span<std::byte> buffer_;
        };


        /// async_write_some
        template<>
        class uring_state_base_for<write_some_tag> : public uring_node_for<write_some_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::span<const std::byte> buffer) noexcept :
                uring_node_for(fd, context),
                buffer_(buffer) {}

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            std::span<const std::byte> buffer_;
        };


        /// async_read_some_at
        template<>
        class uring_state_base_for<read_some_at_tag> : public uring_node_for<read_some_at_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::size_t offset, std::span<std::byte> buffer) noexcept :
                uring_node_for(fd, context),
                offset_(offset),
                buffer_(buffer) {}

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            std::size_t offset_;
            std::span<std::byte> buffer_;
        };


        /// async_write_some_at
        template<>
        class uring_state_base_for<write_some_at_tag> : public uring_node_for<write_some_at_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::size_t offset, std::span<const std::byte> buffer) noexcept :
                uring_node_for(fd, context),
                offset_(offset),
                buffer_(buffer) {}

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            std::size_t offset_;
            std::span<const std::byte> buffer_;
        };


        /// async_receive
        template<>
        class uring_state_base_for<receive_tag> : public uring_node_for<receive_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::span<std::byte> buffer) noexcept :
                uring_node_for(fd, context),
                buffer_(buffer) {}

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            std::span<std::byte> buffer_;
        };


        /// async_send
        template<>
        class uring_state_base_for<send_tag> : public uring_node_for<send_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::span<const std::byte> buffer) noexcept :
                uring_node_for(fd, context),
                buffer_(buffer) {}

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            std::span<const std::byte> buffer_;
        };


        /// async_receive_from
        template<>
        class uring_state_base_for<receive_from_tag> : public uring_node_for<receive_from_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::span<std::byte> buffer) noexcept;

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            auto complete(int cqe_res) noexcept -> void override;

        private:
            // `msg_` stores pointers into `peer_`/`buffer_`: the object must stay at its construction address
            ::sockaddr_storage peer_;
            ::iovec buffer_;
            ::msghdr msg_;
        };


        /// async_send_to
        template<>
        class uring_state_base_for<send_to_tag> : public uring_node_for<send_to_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, std::span<const std::byte> buffer, const endpoint& dest) noexcept;

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            // `msg_` stores pointers into `peer_`/`buffer_`: the object must stay at its construction address
            std::variant<::sockaddr_in, ::sockaddr_in6> peer_;
            ::iovec buffer_;
            ::msghdr msg_;
        };


        /// async_accept
        template<>
        class uring_state_base_for<accept_tag> : public uring_node_for<accept_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context) noexcept : uring_node_for(fd, context) {}

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;
        };


        /// async_connect
        template<>
        class uring_state_base_for<connect_tag> : public uring_node_for<connect_tag> {
        public:
            uring_state_base_for(int fd, uring_context& context, const endpoint& peer) noexcept;

            auto prepare(::io_uring_sqe* sqe) noexcept -> void;

        private:
            std::variant<::sockaddr_in, ::sockaddr_in6> peer_;
        };
    }
}
