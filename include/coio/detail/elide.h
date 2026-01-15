#pragma once
#include <concepts>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace coio::detail {
    template<typename T, typename... Args>
    class elide {
        static_assert(std::is_reference_v<T> and (std::is_reference_v<Args> and ...));
        static_assert(std::invocable<T, Args...> and not std::is_void_v<std::invoke_result_t<T, Args...>>);
    private:
        using result_type = std::invoke_result_t<T, Args...>;
        static constexpr bool is_nothrow_ = std::is_nothrow_invocable_v<T, Args...>;

    public:
        constexpr explicit elide(T t, Args... args) noexcept : t_(std::forward<T>(t)), args_(std::forward<Args>(args)...) {}

        elide(const elide&) = delete;

        ~elide() = default;

        auto operator= (const elide&) -> elide& = delete;

        // ReSharper disable once CppNonExplicitConversionOperator
        COIO_ALWAYS_INLINE constexpr operator result_type() const&& noexcept(is_nothrow_) {
            return std::apply(
                [&](auto&&... args) noexcept(is_nothrow_) {
                    return std::invoke(std::forward<T>(t_), std::forward<Args>(args)...);
                },
                args_
            );
        }

    private:
        COIO_NO_UNIQUE_ADDRESS T t_;
        COIO_NO_UNIQUE_ADDRESS std::tuple<Args...> args_;
    };

    template<typename T, typename... Args>
    explicit elide(T&&, Args&&...) -> elide<T&&, Args&&...>;
}