#pragma once
#include <algorithm>
#include <span>
#include <stop_token>
#include "core.h"
#include "error_code.h"

namespace coio {

    template<typename T>
    concept readable_file = requires (T t, std::span<std::byte> buffer) {
        { t.read_some(buffer) } -> std::integral;
    };

    template<typename T>
    concept writable_file = requires (T t, std::span<const std::byte> buffer) {
        { t.write_some(buffer) } -> std::integral;
    };

    template<typename T>
    concept readable_and_writable_file = readable_file<T> and writable_file<T>;

    template<typename T>
    concept async_readable_file = requires (T t, std::span<std::byte> buffer) {
        { t.async_read_some(buffer) } -> awaitable;
        requires std::integral<detail::awaitable_await_result_t<decltype(t.async_read_some(buffer))>>;
    };

    template<typename T>
    concept async_writable_file = requires (T t, std::span<const std::byte> buffer) {
        { t.async_write_some(buffer) } -> awaitable;
        requires std::integral<detail::awaitable_await_result_t<decltype(t.async_write_some(buffer))>>;
    };

    template<typename T>
    concept async_readable_and_writable_file = async_readable_file<T> and async_writable_file<T>;

    namespace detail {
        struct read_fn {
            COIO_STATIC_CALL_OP auto operator() (readable_file auto&& file, std::span<std::byte> buffer) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t total = buffer.size();
                std::size_t remain = total;
                do {
                    remain -= file.read_some(buffer.subspan(total - remain, remain));
                }
                while (remain > 0);
                return total;
            }
        };

        struct write_fn {
            COIO_STATIC_CALL_OP auto operator() (writable_file auto&& file, std::span<const std::byte> buffer) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t total = buffer.size();
                std::size_t remain = total;
                do {
                    remain -= file.write_some(buffer.subspan(total - remain, remain));
                }
                while (remain > 0);
                return total;
            }
        };

        struct async_read_fn {
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (async_readable_file auto&& file, std::span<std::byte> buffer) COIO_STATIC_CALL_OP_CONST -> task<std::size_t> {
                const std::size_t total = buffer.size();
                std::size_t remain = total;
                do {
                    remain -= co_await file.async_read_some(buffer.subspan(total - remain, remain));
                }
                while (remain > 0);
                co_return total;
            }
        };

        struct async_write_fn {
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (async_writable_file auto&& file, std::span<const std::byte> buffer) COIO_STATIC_CALL_OP_CONST -> task<std::size_t> {
                const std::size_t total = buffer.size();
                std::size_t remain = total;
                do {
                    remain -= co_await file.async_write_some(buffer.subspan(total - remain, remain));
                }
                while (remain > 0);
                co_return total;
            }
        };
    }

    struct operation_stopped : std::exception {
        auto what() const noexcept -> const char* override {
            return "operation stopped";
        }
    };

    inline constexpr detail::read_fn        read{};
    inline constexpr detail::write_fn       write{};
    inline constexpr detail::async_read_fn  async_read{};
    inline constexpr detail::async_write_fn async_write{};

}