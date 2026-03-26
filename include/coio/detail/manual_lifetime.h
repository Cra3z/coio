#pragma once
#include <concepts>
#include <memory>
#include <type_traits>
#include "config.h"

namespace coio::detail {
    template<typename T>
    class manual_lifetime {
    public:
        manual_lifetime() noexcept = default;

        manual_lifetime(const manual_lifetime&) = delete;

        manual_lifetime(manual_lifetime&&) = delete;

        ~manual_lifetime() = default;

        auto operator= (const manual_lifetime&) -> manual_lifetime& = delete;

        auto operator= (manual_lifetime&&) -> manual_lifetime& = delete;

        template<typename... Args> requires std::constructible_from<T, Args...>
        COIO_ALWAYS_INLINE auto construct(Args&&... args) & noexcept(std::is_nothrow_constructible_v<T, Args...>) -> T& {
            return *std::construct_at(reinterpret_cast<T*>(storage_), std::forward<Args>(args)...);
        }

        template<typename Fn, typename... Args> requires std::invocable<Fn, Args...> and std::same_as<T, std::invoke_result_t<Fn, Args...>>
        COIO_ALWAYS_INLINE auto elide_construct(Fn&& fn, Args&&... args) & noexcept(std::is_nothrow_invocable_v<Fn, Args...>) -> T& {
            return *::new(static_cast<void*>(storage_)) T(std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...));
        }

        COIO_ALWAYS_INLINE auto destroy() & noexcept -> void { // pre: `construct` has been called
            std::destroy_at(&get());
        }

        COIO_ALWAYS_INLINE auto get() & noexcept -> T& {
            return *std::launder(reinterpret_cast<T*>(storage_));
        }

    private:
        alignas(T) std::byte storage_[sizeof(T)];
    };
}