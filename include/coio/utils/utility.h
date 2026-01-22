#pragma once
#include <utility>
#include "../detail/config.h"
#include "../detail/concepts.h" // IWYU pragma: keep
#include "type_traits.h" // IWYU pragma: keep

namespace coio {
#ifdef __cpp_lib_unreachable
    using std::unreachable;
#else
    [[noreturn]]
    COIO_ALWAYS_INLINE auto unreachable() -> void {
#if defined(_MSC_VER) && !defined(__clang__)
        __assume(false);
#else
        __builtin_unreachable();
#endif
    }
#endif

    template<typename E> requires std::is_enum_v<E>
    COIO_ALWAYS_INLINE constexpr auto to_underlying(E e) noexcept -> std::underlying_type_t<E> {
        return static_cast<std::underlying_type_t<E>>(e);
    }

    template<typename T>
    COIO_ALWAYS_INLINE constexpr auto to_signed(T x) noexcept -> std::make_signed_t<T> {
        return static_cast<std::make_signed_t<T>>(x);
    }

    template<typename T>
    COIO_ALWAYS_INLINE constexpr auto to_unsigned(T x) noexcept -> std::make_unsigned_t<T> {
        return static_cast<std::make_unsigned_t<T>>(x);
    }

#ifdef __cpp_lib_forward_like
    using std::forward_like;
#else
    template<typename T, typename U>
    COIO_ALWAYS_INLINE constexpr auto&& forward_like(U&& x) noexcept {
        constexpr bool add_const = std::is_const_v<std::remove_reference_t<T>>;
        if constexpr (std::is_lvalue_reference_v<T>) {
            if constexpr (add_const) {
                return std::as_const(x);
            }
            else {
                return static_cast<U&>(x);
            }
        }
        else {
            if constexpr (add_const) {
                return std::move(std::as_const(x));
            }
            else {
                return std::move(x);
            }
        }
    }
#endif
}