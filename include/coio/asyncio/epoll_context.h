// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
#pragma once
#include <coio/detail/config.h>
#if not COIO_HAS_EPOLL
#error "uh, where is <sys/epoll.h>?"
#endif
#include <sys/socket.h>
#include <coio/execution_context.h>
#include <coio/utils/async_result.h>
#include <coio/detail/io_descriptions.h>
#include <coio/detail/object_pool.h>
#include <coio/utils/atomutex.h>

namespace coio {
    namespace detail {
        template<typename Tag>
        class epoll_node_for;

        template<typename Tag>
        class epoll_state_base_for;

        enum class seek_whence;

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
        template<typename Tag>
        friend class detail::epoll_node_for;
        template<typename Tag>
        friend class detail::epoll_state_base_for;
        friend loop_base;
    private:
        class epoll_node;

        struct per_fd_data {
            atomutex fd_lock;
            std::uint32_t events{};
            std::uint32_t ready_events{};
            epoll_node* in_op{nullptr};
            epoll_node* out_op{nullptr};
            per_fd_data* next_free{nullptr};
        };

        class epoll_node : public node {
            friend epoll_context;
        public:
            epoll_node(epoll_context& context, int fd, per_fd_data* data) noexcept : node(context), fd(fd), data(data) {}

        protected:
            enum class register_result : unsigned char {
                armed,
                ready,
                failure
            };

            [[nodiscard]]
            auto register_event(int event_type) noexcept -> register_result;

        private:
            virtual auto perform() noexcept -> bool = 0;

        protected:
            int fd;
            per_fd_data* data;
        };

    public:
        class scheduler : public scheduler_base {
            friend epoll_context;
        public:
            using scheduler_concept = detail::io_scheduler_tag;

            class io_object {
            public:
                io_object(epoll_context& ctx, int fd);

                io_object(const io_object&) = delete;

                io_object(io_object&& other) noexcept :
                    ctx_(other.ctx_),
                    fd_(std::exchange(other.fd_, -1)),
                    stream_oriented_(std::exchange(other.stream_oriented_, false)),
                    data_(std::exchange(other.data_, {})) {}

                ~io_object();

                auto operator= (io_object other) noexcept -> io_object& {
                    swap(other);
                    return *this;
                }

                auto swap(io_object& other) noexcept -> void {
                    std::ranges::swap(ctx_, other.ctx_);
                    std::ranges::swap(fd_, other.fd_);
                    std::ranges::swap(stream_oriented_, other.stream_oriented_);
                    std::ranges::swap(data_, other.data_);
                }

                friend auto swap(io_object& lhs, io_object& rhs) noexcept -> void {
                    lhs.swap(rhs);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler {
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

                auto connect(const endpoint& peer) -> void;

                [[nodiscard]]
                auto accept() -> detail::socket_native_handle_type;

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
                        io_sender<Tag, Args...>{fd_, ctx_, data_, {std::move(args)...}},
                        ctx_->stop_source_.get_token()
                    );
                }

            public:
                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_receive(std::span<std::byte> buffer) noexcept {
                    return async_initiate<detail::receive_tag>(buffer, stream_oriented_);
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
                epoll_context* ctx_;
                int fd_ = -1;
                bool stream_oriented_ = false;
                per_fd_data* data_ = nullptr;
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
                struct state_base : detail::epoll_state_base_for<Tag> {
                    using base = detail::epoll_state_base_for<Tag>;

                    state_base(Rcvr rcvr, int fd, epoll_context& context, per_fd_data* data, Args... args) noexcept :
                        base(fd, context, data, std::move(args)...), rcvr_(std::move(rcvr)) {}

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
                        [&](Args... args_) {
                            return state<Rcvr>{
                                std::move(rcvr),
                                std::exchange(fd, -1),
                                *std::exchange(context, nullptr),
                                std::exchange(data, nullptr),
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
                epoll_context* context;
                per_fd_data* data;
                std::tuple<Args...> args;
            };

        public:
            using scheduler_base::scheduler_base;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto make_io_object(int fd) const -> io_object {
                return io_object{*ctx_, fd};
            }

        public:
            friend auto operator== (const scheduler& lhs, const scheduler& rhs) -> bool = default;
        };

        template<typename T = void, typename Alloc = std::allocator<std::byte>>
        using task = coio::task<T, Alloc, scheduler>;

    public:
        explicit epoll_context(std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());

        ~epoll_context();

    private:
        auto do_one(bool infinite) -> bool;

        COIO_ALWAYS_INLINE auto interrupt() -> void {
            interrupter_.interrupt();
        }

        [[nodiscard]]
        auto new_epoll_data() -> per_fd_data*;

        auto reclaim_epoll_data(per_fd_data* data) noexcept -> void;

        auto cancel_op(int event, epoll_node* op) -> void;

    private:
        // entries are recycled, never freed while the context lives, so straggling
        // references (fetched event batches, stop callbacks) cannot dangle
        detail::object_pool<per_fd_data, &per_fd_data::next_free, std::pmr::polymorphic_allocator<>> data_pool_;
        detail::reactor_interrupter interrupter_;
        int epoll_fd_;
    };

    namespace detail {
        // common per-operation state: links into the reactor and owns the completion result
        template<typename Tag>
        class epoll_node_for : public epoll_context::epoll_node {
        public:
            epoll_node_for(int fd, epoll_context& context, epoll_context::per_fd_data* data) noexcept :
                epoll_node(context, fd, data) {}

        protected:
            async_result<typename Tag::value_signature, execution::set_error_t(std::error_code)> result;
        };

        template<typename Tag>
        class epoll_state_base_for : public epoll_node_for<Tag> {
        public:
            template<typename... Args>
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, const Args&...) noexcept :
                epoll_node_for<Tag>(fd, context, data) {}

        protected:
            auto do_start() noexcept -> start_result {
                static_assert(always_false<Tag>, "this operation isn't supported");
                unreachable();
            }

            auto do_cancel() -> void {
                static_assert(always_false<Tag>, "this operation isn't supported");
            }

        private:
            auto perform() noexcept -> bool override {
                static_assert(always_false<Tag>, "this operation isn't supported");
                unreachable();
            }
        };

        /// async_read_some
        template<>
        class epoll_state_base_for<read_some_tag> : public epoll_node_for<read_some_tag> {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, std::span<std::byte> buffer) noexcept :
                epoll_node_for(fd, context, data),
                buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;

        private:
            std::span<std::byte> buffer_;
        };


        /// async_write_some
        template<>
        class epoll_state_base_for<write_some_tag> : public epoll_node_for<write_some_tag> {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, std::span<const std::byte> buffer) noexcept :
                epoll_node_for(fd, context, data),
                buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;

        private:
            std::span<const std::byte> buffer_;
        };


        /// async_receive
        template<>
        class epoll_state_base_for<receive_tag> : public epoll_node_for<receive_tag> {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, std::span<std::byte> buffer, bool stream_oriented) noexcept :
                epoll_node_for(fd, context, data),
                buffer_(buffer),
                stream_oriented_(stream_oriented) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;

        private:
            std::span<std::byte> buffer_;
            bool stream_oriented_;
        };

        /// async_send
        template<>
        class epoll_state_base_for<send_tag> : public epoll_node_for<send_tag> {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, std::span<const std::byte> buffer) noexcept :
                epoll_node_for(fd, context, data),
                buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;

        private:
            std::span<const std::byte> buffer_;
        };


        /// async_receive_from
        template<>
        class epoll_state_base_for<receive_from_tag> : public epoll_node_for<receive_from_tag> {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, std::span<std::byte> buffer) noexcept :
                epoll_node_for(fd, context, data),
                peer_{},
                buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;

        private:
            ::sockaddr_storage peer_;
            std::span<std::byte> buffer_;
        };


        /// async_send_to
        template<>
        class epoll_state_base_for<send_to_tag> : public epoll_node_for<send_to_tag> {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, std::span<const std::byte> buffer, const endpoint& peer) noexcept :
                epoll_node_for(fd, context, data),
                peer_(peer),
                buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;

        private:
            endpoint peer_;
            std::span<const std::byte> buffer_;
        };


        /// async_accept
        template<>
        class epoll_state_base_for<accept_tag> : public epoll_node_for<accept_tag> {
        public:
            using epoll_node_for::epoll_node_for;

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;
        };


        /// async_connect
        template<>
        class epoll_state_base_for<connect_tag> : public epoll_node_for<connect_tag> {
        public:
            epoll_state_base_for(int fd, epoll_context& context, epoll_context::per_fd_data* data, const endpoint& peer) noexcept :
                epoll_node_for(fd, context, data),
                peer_(peer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto do_cancel() -> void;

        private:
            auto perform() noexcept -> bool override;

        private:
            endpoint peer_;
        };
    }
}
