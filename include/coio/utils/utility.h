#pragma once
#include <utility>
#include "../config.h"
#include "../concepts.h"
#include "type_traits.h"

namespace coio {

#ifdef __cpp_lib_unreachable
    using std::unreachable;
#else
    [[noreturn]]
    inline auto unreachable() -> void {
#if defined(_MSC_VER) && !defined(__clang__)
        __assume(false);
#else
        __builtin_unreachable();
#endif
    }
#endif

    template<typename E> requires std::is_enum_v<E>
    constexpr auto to_underlying(E e) noexcept -> std::underlying_type_t<E> {
        return static_cast<std::underlying_type_t<E>>(e);
    }

}