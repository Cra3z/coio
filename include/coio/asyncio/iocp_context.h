// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <coio/detail/config.h>
#if not COIO_HAS_IOCP
#error "IOCP is not available"
#endif

#include <basetsd.h>
#include <WinSock2.h>
#include <algorithm>
#include <span>
#include <tuple>
#include <coio/execution_context.h>
#include <coio/utils/async_result.h>
#include <coio/detail/io_descriptions.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio {
    namespace detail {
        template<typename Tag>
        class iocp_state_base_for;

        enum class seek_whence;
    }

    class iocp_context : public detail::loop_base<iocp_context> {
        template<typename Tag>
        friend class detail::iocp_state_base_for;
        friend loop_base;

    private:
        static constexpr ::ULONG_PTR wake_completion_key = 1;

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        struct iocp_node : ::OVERLAPPED, node {
            friend iocp_context;
        public:
            explicit iocp_node(iocp_context& context, ::HANDLE handle) noexcept : ::OVERLAPPED{}, node(context), handle(handle) {}

        protected:
            virtual auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void = 0;

            auto do_cancel() -> void;

        protected:
            ::HANDLE handle;
        };

    public:
        class scheduler : public scheduler_base {
            friend iocp_context;
        public:
            using scheduler_concept = detail::io_scheduler_tag;

            template<typename Tag, typename... Args>
            struct io_sender {
                using sender_concept = execution::sender_tag;
                using completion_signatures = execution::completion_signatures<
                    typename Tag::value_signature,
                    execution::set_error_t(std::error_code),
                    execution::set_stopped_t()
                >;

                template<typename Rcvr>
                struct state_base : detail::iocp_state_base_for<Tag> {
                    using base = detail::iocp_state_base_for<Tag>;

                    template<typename... CtorArgs>
                    state_base(Rcvr rcvr, CtorArgs&&... ctor_args) noexcept
                        : base(std::forward<CtorArgs>(ctor_args)...), rcvr_(std::move(rcvr)) {}

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
                        [&]<typename... FwdArgs>(FwdArgs&&... fwd_args) {
                            return state<Rcvr>{
                                std::move(rcvr),
                                std::exchange(handle, INVALID_HANDLE_VALUE),
                                *std::exchange(context, nullptr),
                                std::forward<FwdArgs>(fwd_args)...
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

                ::HANDLE handle;
                iocp_context* context;
                std::tuple<Args...> args;
            };

            class io_object {
                friend scheduler;
            public:
                io_object(const io_object&) = delete;

                auto cancel() -> void;

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler {
                    COIO_ASSERT(ctx_ != nullptr);
                    return scheduler{*ctx_};
                }

            protected:
                io_object(iocp_context& ctx, ::HANDLE handle);

                io_object(io_object&& other) noexcept :
                    ctx_(other.ctx_),
                    handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE))
                {}

                ~io_object() = default;

                auto operator= (io_object other) noexcept -> io_object& {
                    swap_handle(other);
                    return *this;
                }

                auto swap_handle(io_object& other) noexcept -> void {
                    std::ranges::swap(ctx_, other.ctx_);
                    std::ranges::swap(handle_, other.handle_);
                }

                template<typename Tag, typename... Args>
                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_initiate(Args... args) noexcept {
                    return stop_when(
                        io_sender<Tag, Args...>{handle_, ctx_, std::tuple<Args...>{std::move(args)...}},
                        ctx_->stop_source_.get_token()
                    );
                }

            protected:
                iocp_context* ctx_;
                ::HANDLE handle_ = INVALID_HANDLE_VALUE;
            };

            class file_object final : public io_object {
                friend scheduler;
            public:
                file_object(iocp_context& ctx, ::HANDLE handle);

                file_object(file_object&& other) noexcept :
                    io_object(std::move(other)),
                    offset_(std::exchange(other.offset_, 0))
                {}

                ~file_object();

                auto operator= (file_object other) noexcept -> file_object& {
                    swap(other);
                    return *this;
                }

                auto swap(file_object& other) noexcept -> void {
                    std::ranges::swap(ctx_, other.ctx_);
                    std::ranges::swap(handle_, other.handle_);
                    std::ranges::swap(offset_, other.offset_);
                }

                friend auto swap(file_object& lhs, file_object& rhs) noexcept -> void {
                    lhs.swap(rhs);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto native_handle() const noexcept -> ::HANDLE {
                    return handle_;
                }

                auto close() -> void;

                auto read_some(std::span<std::byte> buffer) -> std::size_t;

                auto write_some(std::span<const std::byte> buffer) -> std::size_t;

                auto read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t;

                auto write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t;

                auto seek(std::size_t offset, detail::seek_whence whence) -> std::size_t;

                auto resize(std::size_t new_size) -> void;

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_read_some(std::span<std::byte> buffer) noexcept {
                    const auto length = std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu);
                    return async_initiate<detail::read_some_at_tag>(std::exchange(offset_, offset_ + length), buffer);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto async_write_some(std::span<const std::byte> buffer) noexcept {
                    const auto length = std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu);
                    return async_initiate<detail::write_some_at_tag>(std::exchange(offset_, offset_ + length), buffer);
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
                std::size_t offset_ = 0; // for `stream_file`
            };

            class socket_object final : public io_object {
                friend scheduler;
            public:
                socket_object(iocp_context& ctx, detail::socket_native_handle_type sock);

                socket_object(socket_object&& other) noexcept : io_object(std::move(other)) {}

                ~socket_object();

                auto operator= (socket_object other) noexcept -> socket_object& {
                    swap(other);
                    return *this;
                }

                auto swap(socket_object& other) noexcept -> void {
                    swap_handle(other);
                }

                friend auto swap(socket_object& lhs, socket_object& rhs) noexcept -> void {
                    lhs.swap(rhs);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto native_handle() const noexcept -> ::SOCKET {
                    return std::bit_cast<::SOCKET>(handle_);
                }

                auto close() -> void;

                [[nodiscard]]
                auto receive(std::span<std::byte> buffer) -> std::size_t;

                [[nodiscard]]
                auto send(std::span<const std::byte> buffer) -> std::size_t;

                [[nodiscard]]
                auto receive_from(std::span<std::byte> buffer) -> std::pair<endpoint, std::size_t>;

                [[nodiscard]]
                auto send_to(std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t;

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
            };

        public:
            using scheduler_base::scheduler_base;

            [[nodiscard]]
            auto make_io_object(::HANDLE handle) const -> file_object;

            [[nodiscard]]
            auto make_io_object(::SOCKET sock) const -> socket_object;

            friend auto operator== (const scheduler& lhs, const scheduler& rhs) -> bool = default;
        };

        template<typename T = void, typename Alloc = void>
        using task = coio::task<T, Alloc, scheduler>;

    public:
        explicit iocp_context(std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());

        iocp_context(const iocp_context&) = delete;

        ~iocp_context();

        auto operator= (const iocp_context&) -> iocp_context& = delete;

    private:
        auto do_one(bool infinite) -> bool;

        auto interrupt() -> void;

    private:
        ::HANDLE iocp_;
    };

    namespace detail {
        /// async_read_some_at
        template<>
        class iocp_state_base_for<read_some_at_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx, std::size_t offset, std::span<std::byte> buffer) noexcept
                : iocp_node(ctx, handle_), offset_(offset), buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<read_some_at_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            std::size_t offset_;
            std::span<std::byte> buffer_;
        };

        /// async_write_some_at
        template<>
        class iocp_state_base_for<write_some_at_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx, std::size_t offset, std::span<const std::byte> buffer) noexcept
                : iocp_node(ctx, handle_), offset_(offset), buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<write_some_at_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            std::size_t offset_;
            std::span<const std::byte> buffer_;
        };

        /// async_receive
        template<>
        class iocp_state_base_for<receive_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx, std::span<std::byte> buffer) noexcept
                : iocp_node(ctx, handle_), buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<receive_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            std::span<std::byte> buffer_;
        };

        /// async_send
        template<>
        class iocp_state_base_for<send_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx, std::span<const std::byte> buffer) noexcept
                : iocp_node(ctx, handle_), buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<send_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            std::span<const std::byte> buffer_;
        };

        /// async_receive_from
        template<>
        class iocp_state_base_for<receive_from_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx, std::span<std::byte> buffer) noexcept
                : iocp_node(ctx, handle_), buffer_(buffer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<receive_from_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            std::span<std::byte> buffer_;
            ::sockaddr_storage peer_storage_{};
            int peer_length_ = sizeof(::sockaddr_storage);
        };

        /// async_send_to
        template<>
        class iocp_state_base_for<send_to_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx, std::span<const std::byte> buffer, endpoint dest) noexcept
                : iocp_node(ctx, handle_), buffer_(buffer), dest_(dest) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<send_to_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            std::span<const std::byte> buffer_;
            endpoint dest_;
        };

        /// async_accept
        template<>
        class iocp_state_base_for<accept_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx) noexcept : iocp_node(ctx, handle_) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<accept_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            socket_native_handle_type accepted_ = invalid_socket_handle;
            std::byte output_buffer_[2 * (sizeof(::sockaddr_storage) + 16)]{};
        };

        /// async_connect
        template<>
        class iocp_state_base_for<connect_tag> : public iocp_context::iocp_node {
        public:
            iocp_state_base_for(::HANDLE handle_, iocp_context& ctx, endpoint peer) noexcept
                : iocp_node(ctx, handle_), peer_(peer) {}

        protected:
            auto do_start() noexcept -> start_result;

            auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void final;

        protected:
            async_result<connect_tag::value_signature, execution::set_error_t(std::error_code)> result;

        private:
            endpoint peer_;
        };
    }
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
