// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include "../detail/config.h"
#if not COIO_HAS_IOCP
#error "IOCP is not available"
#endif

#include <BaseTsd.h>
#include <WinSock2.h>
#undef min
#undef max

#include "../execution_context.h"
#include "../detail/async_result.h"
#include "../detail/io_descriptions.h"
#include "file.h"

namespace coio {
    namespace detail {
        template<typename Sexpr>
        class iocp_state_base_for;
    }

    class iocp_context : public detail::loop_base<iocp_context> {
        template<typename Sexpr>
        friend class detail::iocp_state_base_for;
        friend loop_base;

    private:
        static constexpr ::ULONG_PTR wake_completion_key = 1;

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        struct iocp_node : ::OVERLAPPED, node {
            explicit iocp_node(iocp_context& context, ::HANDLE handle) noexcept : ::OVERLAPPED{}, node(context), handle(handle) {}

            virtual auto complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void = 0;

            auto do_cancel() -> void;

            ::HANDLE handle;
        };

    public:
        class scheduler : public scheduler_base {
            friend iocp_context;
        public:
            using scheduler_concept = detail::io_scheduler_t;

            class io_object {
                friend scheduler;
            private:
                struct handle_wrapper {
                    detail::file_native_handle_type handle;

                    // ReSharper disable once CppNonExplicitConversionOperator
                    COIO_ALWAYS_INLINE operator detail::file_native_handle_type() const noexcept {
                        return handle;
                    }

                    // ReSharper disable once CppNonExplicitConversionOperator
                    COIO_ALWAYS_INLINE operator detail::socket_native_handle_type() const noexcept {
                        return std::bit_cast<detail::socket_native_handle_type>(handle);
                    }
                };

            public:
                io_object(iocp_context& ctx, HANDLE handle);

                io_object(const io_object&) = delete;

                io_object(io_object&& other) noexcept :
                    ctx_(other.ctx_),
                    handle_(std::exchange(other.handle_, detail::invalid_file_handle)) {}

                ~io_object();

                auto operator= (io_object other) noexcept -> io_object& {
                    std::ranges::swap(ctx_, other.ctx_);
                    std::ranges::swap(handle_, other.handle_);
                    return *this;
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler {
                    COIO_ASSERT(ctx_ != nullptr);
                    return scheduler{*ctx_};
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto native_handle() const noexcept {
                    return handle_wrapper{handle_};
                }

                auto release() {
                    if (handle_ != detail::invalid_file_handle) cancel();
                    return handle_wrapper{std::exchange(handle_, detail::invalid_file_handle)};
                }

                auto cancel() -> void;

            private:
                iocp_context* ctx_;
                detail::file_native_handle_type handle_ = detail::invalid_file_handle;
                std::size_t offset_ = 0; // for `stream_file`
            };

            template<std::move_constructible Sexpr>
            struct io_sender {
                using sender_concept = execution::sender_t;
                using completion_signatures = execution::completion_signatures<
                    typename Sexpr::value_signature,
                    execution::set_error_t(std::error_code),
                    execution::set_stopped_t()
                >;

                template<typename Rcvr>
                struct state_base : detail::iocp_state_base_for<Sexpr> {
                    using base = detail::iocp_state_base_for<Sexpr>;

                    template<typename... Args>
                    state_base(Rcvr rcvr, Args&&... args) noexcept
                        : base(std::forward<Args>(args)...), rcvr_(std::move(rcvr)) {}

                    COIO_ALWAYS_INLINE auto do_finish(bool) noexcept -> void {
                        this->result.forward_to(std::move(this->rcvr_));
                    }

                    Rcvr rcvr_;
                };

                template<typename Rcvr>
                using state = operation_state<state_base<Rcvr>>;

                template<execution::receiver_of<completion_signatures> Rcvr>
                COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                    COIO_ASSERT(context != nullptr);
                    return state<Rcvr>{
                        std::move(rcvr),
                        std::exchange(handle, detail::invalid_file_handle),
                        *std::exchange(context, nullptr),
                        std::move(sexpr)
                    };
                }

                template<std::same_as<io_sender>, typename...>
                static consteval auto get_completion_signatures() noexcept -> completion_signatures {
                    return {};
                }

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*context};
                }

                detail::file_native_handle_type handle;
                iocp_context* context;
                Sexpr sexpr;
            };

        public:
            using scheduler_base::scheduler_base;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto make_io_object(detail::file_native_handle_type handle) const -> io_object {
                return io_object{*ctx_, handle};
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto make_io_object(detail::socket_native_handle_type sock) const -> io_object {
                return io_object{*ctx_, std::bit_cast<detail::file_native_handle_type>(sock)};
            }

            template<typename Sexpr>
            COIO_ALWAYS_INLINE static auto transform_sexpr(io_object& obj, Sexpr sexpr) noexcept {
                if constexpr (not std::same_as<Sexpr, detail::async_read_some_t> and not std::same_as<Sexpr, detail::async_write_some_t>) {
                    return std::move(sexpr);
                }
                else {
                    using result_t = std::conditional_t<
                        std::same_as<Sexpr, detail::async_read_some_t>,
                        detail::async_read_some_at_t,
                        detail::async_write_some_at_t
                    >;
                    return result_t{
                        .offset = std::exchange(obj.offset_, obj.offset_ + sexpr.buffer.size()),
                        .buffer = sexpr.buffer
                    };
                }
            }

            template<typename Sexpr>
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto schedule_io(io_object& obj, Sexpr sexpr) noexcept {
                using transformed_sexpr_t = decltype(transform_sexpr(obj, std::move(sexpr)));
                return stop_when(
                    io_sender<transformed_sexpr_t>{
                        obj.handle_,
                        ctx_,
                        transform_sexpr(obj, std::move(sexpr))
                    },
                    ctx_->stop_source_.get_token()
                );
            }

            friend auto operator== (const scheduler& lhs, const scheduler& rhs) -> bool = default;
        };

    public:
        explicit iocp_context(std::pmr::memory_resource& memory_resource = *std::pmr::get_default_resource());

        iocp_context(const iocp_context&) = delete;

        ~iocp_context();

        auto operator= (const iocp_context&) -> iocp_context& = delete;

    private:
        auto do_one(bool infinite) -> bool;

        auto interrupt() -> void;

    private:
        detail::file_native_handle_type iocp_;
        std::mutex bolt_;
    };

    namespace detail {
        template<typename Sexpr>
        struct iocp_sexpr_wrapper {
            using type = Sexpr;
        };

        template<>
        struct iocp_sexpr_wrapper<async_receive_from_t> {
            struct type : async_receive_from_t {
                explicit type(async_receive_from_t s) noexcept : async_receive_from_t(std::move(s)) {}

                ::sockaddr_storage peer_storage{};
                int peer_length = sizeof(::sockaddr_storage);
            };
        };

        template<>
        struct iocp_sexpr_wrapper<async_accept_t> {
            struct type : async_accept_t {
                explicit type(async_accept_t s) noexcept : async_accept_t(s) {}

                socket_native_handle_type accepted = invalid_socket_handle;
                std::byte output_buffer[2 * (sizeof(::sockaddr_storage) + 16)]{};
            };
        };

        template<typename Sexpr>
        class iocp_state_base_for :
            private iocp_sexpr_wrapper<Sexpr>::type,
            public iocp_context::iocp_node {
        private:
            using native_type = typename iocp_sexpr_wrapper<Sexpr>::type;

        public:
            iocp_state_base_for(HANDLE handle, iocp_context& ctx, Sexpr sexpr) noexcept
                : native_type(std::move(sexpr)), iocp_node(ctx, handle) {}

        protected:
            auto do_start() noexcept -> bool {
                static_assert(always_false<Sexpr>, "this operation isn't supported");
                unreachable();
            }

            auto complete(::DWORD, ::DWORD) noexcept -> void final {
                static_assert(always_false<Sexpr>, "this operation isn't supported");
            }

        protected:
            async_result<typename Sexpr::value_signature, execution::set_error_t(std::error_code)> result;
        };

        /// async_read_some
        template<>
        auto iocp_state_base_for<async_read_some_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_read_some_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_write_some
        template<>
        auto iocp_state_base_for<async_write_some_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_write_some_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_read_some_at
        template<>
        auto iocp_state_base_for<async_read_some_at_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_read_some_at_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_write_some_at
        template<>
        auto iocp_state_base_for<async_write_some_at_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_write_some_at_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_receive
        template<>
        auto iocp_state_base_for<async_receive_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_receive_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_send
        template<>
        auto iocp_state_base_for<async_send_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_send_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_receive_from
        template<>
        auto iocp_state_base_for<async_receive_from_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_receive_from_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_send_to
        template<>
        auto iocp_state_base_for<async_send_to_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_send_to_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_accept
        template<>
        auto iocp_state_base_for<async_accept_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_accept_t>::complete(::DWORD, ::DWORD) noexcept -> void;

        /// async_connect
        template<>
        auto iocp_state_base_for<async_connect_t>::do_start() noexcept -> bool;

        template<>
        auto iocp_state_base_for<async_connect_t>::complete(::DWORD, ::DWORD) noexcept -> void;
    }
}
