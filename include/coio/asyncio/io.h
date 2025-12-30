#pragma once
#include <span>
#include <stop_token>  // IWYU pragma: keep
#include "../task.h"
#include "../error.h" // IWYU pragma: keep

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
        { t.async_read_some(buffer) } -> awaitable_value;
        requires std::integral<detail::await_result_t<decltype(t.async_read_some(buffer))>>;
    };

    template<typename T>
    concept async_writable_file = requires (T t, std::span<const std::byte> buffer) {
        { t.async_write_some(buffer) } -> awaitable_value;
        requires std::integral<detail::await_result_t<decltype(t.async_write_some(buffer))>>;
    };

    template<typename T>
    concept async_readable_and_writable_file = async_readable_file<T> and async_writable_file<T>;

    template<typename T>
    concept dynamic_buffer = std::constructible_from<T, T> and requires (T t, const T& ct, std::size_t n) {
        { ct.size() } -> std::integral;
        { ct.capacity() } -> std::integral;
        { ct.max_size() } -> std::integral;
        { ct.data() } -> std::convertible_to<std::span<const std::byte>>;
        { t.prepare(n) } -> std::convertible_to<std::span<std::byte>>;
        t.commit(n);
        t.consume(n);
    };

    namespace detail {
        struct read_fn {
            COIO_STATIC_CALL_OP auto operator() (readable_file auto&& file, std::span<std::byte> buffer) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t total = buffer.size();
                if (total == 0) return 0;
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
                if (total == 0) return 0;
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
            COIO_STATIC_CALL_OP auto operator() (async_readable_file auto&& file, std::span<std::byte> buffer) COIO_STATIC_CALL_OP_CONST {
                return async_read_fn{}(std::allocator_arg, std::allocator<void>{}, std::forward<decltype(file)>(file), buffer);
            }

            template<typename Alloc>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (std::allocator_arg_t, const Alloc&, async_readable_file auto&& file, std::span<std::byte> buffer) COIO_STATIC_CALL_OP_CONST -> task<std::size_t, Alloc> {
                const std::size_t total = buffer.size();
                if (total == 0) co_return 0;
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
            COIO_STATIC_CALL_OP auto operator() (async_writable_file auto&& file, std::span<const std::byte> buffer) COIO_STATIC_CALL_OP_CONST {
                return async_write_fn{}(std::allocator_arg, std::allocator<void>{}, std::forward<decltype(file)>(file), buffer);
            }

            template<typename Alloc>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (std::allocator_arg_t, const Alloc&, async_writable_file auto&& file, std::span<const std::byte> buffer) COIO_STATIC_CALL_OP_CONST -> task<std::size_t, Alloc> {
                const std::size_t total = buffer.size();
                if (total == 0) co_return 0;
                std::size_t remain = total;
                do {
                    remain -= co_await file.async_write_some(buffer.subspan(total - remain, remain));
                }
                while (remain > 0);
                co_return total;
            }
        };

        struct as_bytes_fn {
            template<typename... Args> requires requires { std::span(std::declval<Args>()...); }
            [[nodiscard]]
            COIO_ALWAYS_INLINE
            COIO_STATIC_CALL_OP
            auto operator() (Args&&... args) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(std::span(std::declval<Args>()...)))
            -> std::span<const std::byte> {
                return std::as_bytes(std::span(std::forward<Args>(args)...));
            }
        };

        struct as_writable_bytes_fn {
            template<typename... Args> requires requires { std::span(std::declval<Args>()...); }
            [[nodiscard]]
            COIO_ALWAYS_INLINE
            COIO_STATIC_CALL_OP
            auto operator() (Args&&... args) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(std::span(std::declval<Args>()...)))
            -> std::span<std::byte> {
                return std::as_writable_bytes(std::span(std::forward<Args>(args)...));
            }
        };
    }

    inline constexpr detail::read_fn              read{};
    inline constexpr detail::write_fn             write{};
    inline constexpr detail::async_read_fn        async_read{};
    inline constexpr detail::async_write_fn       async_write{};
    inline constexpr detail::as_bytes_fn          as_bytes{};
    inline constexpr detail::as_writable_bytes_fn as_writable_bytes{};
}