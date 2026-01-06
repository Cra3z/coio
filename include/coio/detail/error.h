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

    struct misc_category_t : std::error_category {
        auto name() const noexcept -> const char* override {
            return "coio::net::error::misc";
        }

        auto message(int ec) const -> std::string override {
            switch (ec) {
            case eof:
                return "end of file";
            case already_open:
                return "already open";
            case not_found:
                return "not found";
            case overflow:
                return "overflow";
            default: unreachable();
            }
        }
    };

    struct gai_category_t : std::error_category { // for `getaddrinfo`
        auto name() const noexcept -> const char* override {
            return "coio::net::error::gai";
        }

        auto message(int ec) const -> std::string override;
    };

    [[nodiscard]]
    inline auto misc_category() noexcept -> const std::error_category& {
        static misc_category_t instance;
        return instance;
    }

    [[nodiscard]]
    inline auto make_error_code(misc_errc e) -> std::error_code {
        return {static_cast<int>(e), misc_category()};
    }

    [[nodiscard]]
    inline auto gai_category() noexcept -> const std::error_category& {
        static gai_category_t instance;
        return instance;
    }

}

template<>
struct std::is_error_code_enum<coio::error::misc_errc> : std::true_type {};