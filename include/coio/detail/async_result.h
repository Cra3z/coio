#pragma once
#include <exception>
#include <type_traits>
#include <variant>
#include "config.h"

namespace coio::detail {
    template<typename T>
    class async_result {
    public:
        async_result() = default;

        template<typename... Args> requires (std::is_void_v<T> and sizeof...(Args) == 0) or (std::constructible_from<T, Args...>)
        auto value(Args&&... args) noexcept(std::is_nothrow_move_assignable_v<T>) {
            result_.template emplace<1>(std::forward<Args>(args)...);
        }

        auto error(std::error_code ec) noexcept -> void {
            COIO_ASSERT(ec);
            result_.template emplace<2>(std::move(ec));
        }

        auto ready() const noexcept -> bool {
            return result_.index() > 0;
        }

        [[nodiscard]]
        auto get(const char* error_message) -> T {
            COIO_ASSERT(result_.index() != 0);
            if (result_.index() == 1) {
                return T(*std::get_if<1>(&result_));
            }
            throw std::system_error{*std::get_if<2>(&result_), error_message};
        }

    private:
        std::variant<
            std::monostate,
            std::conditional_t<std::is_void_v<T>, std::nullptr_t, T>,
            std::error_code
        > result_;
    };
}