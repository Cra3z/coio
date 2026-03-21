#pragma once
#include <span>
#include <system_error>
#include <utility>
#include "context.h"
#include "../task.h"
#include "../utils/zstring_view.h"

#if not COIO_HAS_SSL
#error "coio::ssl requires building coio with COIO_WITH_SSL=ON"
#endif

typedef struct ssl_st SSL;
typedef struct bio_st BIO;

namespace coio::ssl {
    namespace detail {
         // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        class stream_base {
        public:
            using native_handle_type = ::SSL*;

        protected:
            explicit stream_base(context& ctx);

            stream_base(const stream_base&) = delete;

            stream_base(stream_base&& other) noexcept : ssl_(std::exchange(other.ssl_, nullptr)) {}

            // ReSharper disable once CppHiddenFunction
            ~stream_base();

            auto operator= (const stream_base&) -> stream_base& = delete;

            auto operator= (stream_base&& other) noexcept -> stream_base& {
                if (this != &other) [[likely]] ssl_ = std::exchange(other.ssl_, nullptr);
                return *this;
            }

        public:
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto native_handle() const noexcept -> native_handle_type {
                return ssl_;
            }

            auto set_verify_mode(verify_mode mode) -> void;

            auto set_verify_depth(int depth) -> void;

            auto set_server_name(zstring_view host) -> void;

            auto set_verify_host_name(zstring_view host) -> void;

            auto set_host_name(zstring_view host) -> void;

            auto handshake(handshake_type type) -> void;

            auto async_handshake(handshake_type type) -> task<>;

            auto shutdown() -> void;

            auto async_shutdown() -> task<>;

            [[nodiscard]]
            auto read_some(std::span<std::byte> buffer) -> std::size_t;

            [[nodiscard]]
            auto write_some(std::span<const std::byte> buffer) -> std::size_t;

        protected:
            [[nodiscard]]
            auto async_read_some_impl(std::span<std::byte> buffer) -> task<std::size_t>;

            [[nodiscard]]
            auto async_write_some_impl(std::span<const std::byte> buffer) -> task<std::size_t>;

        private:
            virtual auto transport_read_some(std::span<std::byte> buffer) -> std::size_t = 0;

            virtual auto transport_write_some(std::span<const std::byte> buffer) -> std::size_t = 0;

            virtual auto async_transport_read_some(std::span<std::byte> buffer) -> task<std::size_t> = 0;

            virtual auto async_transport_write_some(std::span<const std::byte> buffer) -> task<std::size_t> = 0;

            auto set_handshake_state(handshake_type type) -> void;

            auto flush_output() -> void;

            auto async_flush_output() -> task<>;

            auto fill_input() -> void;

            auto async_fill_input() -> task<>;

            auto push_input_bytes(std::span<const std::byte> buffer) -> void;

        private:
            SSL* ssl_;
        };
    }

    template<typename NextLayer>
    class stream final : public detail::stream_base {
    public:
        using next_layer_type = NextLayer;

        stream(NextLayer next_layer, context& ctx) : detail::stream_base(ctx), next_layer_(std::move(next_layer)) {}

        stream(const stream&) = delete;

        stream(stream&&) noexcept = default;

        ~stream() = default;

        auto operator= (const stream&) -> stream& = delete;

        auto operator= (stream&&) noexcept -> stream& = default;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto next_layer() noexcept -> NextLayer& {
            return next_layer_;
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto next_layer() const noexcept -> const NextLayer& {
            return next_layer_;
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE decltype(auto) lowest_layer() noexcept {
            if constexpr (requires (NextLayer& layer) { layer.lowest_layer(); }) {
                return next_layer_.lowest_layer();
            }
            else {
                return (next_layer_);
            }
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE decltype(auto) lowest_layer() const noexcept {
            if constexpr (requires (const NextLayer& layer) { layer.lowest_layer(); }) {
                return next_layer_.lowest_layer();
            }
            else {
                return (next_layer_);
            }
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE decltype(auto) get_io_scheduler() const noexcept {
            return next_layer_.get_io_scheduler();
        }

        [[nodiscard]]
        auto async_read_some(std::span<std::byte> buffer) {
            return execution::let_error(detail::stream_base::async_read_some_impl(buffer), [](const std::exception_ptr& error) noexcept {
                try {
                    std::rethrow_exception(error);
                }
                catch (const std::system_error& e) {
                    return execution::just_error(e.code());
                }
                catch (...) {
                    return execution::just_error(std::make_error_code(std::errc::io_error));
                }
            });
        }

        [[nodiscard]]
        auto async_write_some(std::span<const std::byte> buffer) {
            return execution::let_error(detail::stream_base::async_write_some_impl(buffer), [](const std::exception_ptr& error) noexcept {
                try {
                    std::rethrow_exception(error);
                }
                catch (const std::system_error& e) {
                    return execution::just_error(e.code());
                }
                catch (...) {
                    return execution::just_error(std::make_error_code(std::errc::io_error));
                }
            });
        }

    private:
        auto transport_read_some(std::span<std::byte> buffer) -> std::size_t override {
            return next_layer_.read_some(buffer);
        }

        auto transport_write_some(std::span<const std::byte> buffer) -> std::size_t override {
            return next_layer_.write_some(buffer);
        }

        auto async_transport_read_some(std::span<std::byte> buffer) -> task<std::size_t> override {
            co_return co_await next_layer_.async_read_some(buffer);
        }

        auto async_transport_write_some(std::span<const std::byte> buffer) -> task<std::size_t> override {
            co_return co_await next_layer_.async_write_some(buffer);
        }

        NextLayer next_layer_;
    };
}