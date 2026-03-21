#pragma once
#include <stdexcept> // IWYU pragma: keep
#include <system_error>
#include "../utils/utility.h"

namespace coio::error {
    enum misc_errc : int {
        eof = 1,
        already_open,
        not_found,
        overflow
    };

    [[nodiscard]]
    auto misc_category() noexcept -> const std::error_category&;

    [[nodiscard]]
    COIO_ALWAYS_INLINE auto make_error_code(misc_errc e) noexcept -> std::error_code {
        return {static_cast<int>(e), misc_category()};
    }

    [[nodiscard]]
    auto gai_category() noexcept -> const std::error_category&; // for `getaddrinfo`

#if COIO_HAS_SSL
    [[nodiscard]]
    auto ssl_category() noexcept -> const std::error_category&;
#endif
}

template<>
struct std::is_error_code_enum<coio::error::misc_errc> : std::true_type {};