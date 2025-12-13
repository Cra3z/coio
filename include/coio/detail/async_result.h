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

        auto error(std::exception_ptr e) noexcept {
            COIO_ASSERT(e != nullptr);
            result_.template emplace<2>(std::move(e));
        }

        auto ready() const noexcept -> bool {
            return result_.index() > 0;
        }

        [[nodiscard]]
        auto get() -> T {
            COIO_ASSERT(result_.index() != 0);
            if (result_.index() == 1) {
                return T(*std::get_if<1>(&result_));
            }
            auto exp =*std::get_if<2>(&result_);
            COIO_ASSERT(exp != nullptr);
            std::rethrow_exception(exp);
        }

    private:
        std::variant<std::monostate, std::conditional_t<std::is_void_v<T>, std::nullptr_t, T>, std::exception_ptr> result_;
    };
}