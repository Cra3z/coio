#pragma once
#include <concepts>
#include <memory>
#include <type_traits>
#include "config.h"

namespace coio::detail {
    template<typename T>
    class manual_lifetime {
        static_assert(std::is_object_v<T> and not std::is_array_v<T>);
    public:
        manual_lifetime() noexcept = default;

        manual_lifetime(const manual_lifetime&) = delete;

        ~manual_lifetime() noexcept = default;

        auto operator= (const manual_lifetime&) -> manual_lifetime& = delete;

        template<typename... Args> requires std::constructible_from<T, Args...>
        COIO_ALWAYS_INLINE auto construct(Args&&... args) & noexcept(std::is_nothrow_constructible_v<T, Args...>) -> void {
            std::construct_at(reinterpret_cast<T*>(storage_), std::forward<Args>(args)...);
        }

        COIO_ALWAYS_INLINE auto destroy() & noexcept -> void {
            std::destroy_at(std::addressof(get()));
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get() & noexcept -> T& {
            return *std::launder(reinterpret_cast<T*>(storage_));
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get() const& noexcept -> const T& {
            return const_cast<manual_lifetime*>(this)->get();
        }

    private:
        alignas(T) std::byte storage_[sizeof(T)];
    };
}