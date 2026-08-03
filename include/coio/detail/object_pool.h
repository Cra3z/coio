#pragma once
#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <coio/detail/config.h>
#include <coio/utils/atomutex.h>
#include <coio/utils/new_object.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio::detail {
    template<typename T, auto NextAccessor, typename Allocator = std::allocator<T>>
    class object_pool {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::same_as<std::remove_cv_t<T>, T>);
        static_assert(std::is_nothrow_invocable_r_v<T*, decltype(NextAccessor), T*>);
    public:
        using value_type = T;
        using allocator_type = std::allocator_traits<Allocator>::template rebind_alloc<T>;

    public:
        object_pool() = default;

        explicit object_pool(Allocator alloc) noexcept : alloc_(alloc) {}

        object_pool(const object_pool&) = delete;

        /// \note: All calls to `acquire` or `release` shall happen before this destructor!
        ~object_pool() {
            while (free_list_ != nullptr) {
                T* entry = std::exchange(free_list_, std::invoke(NextAccessor, free_list_));
                coio::delete_object(alloc_, entry);
            }
        }

        auto operator= (const object_pool&) -> object_pool& = delete;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto acquire() -> T* {
            {
                std::scoped_lock _{lock_};
                if (free_list_ != nullptr) {
                    return std::exchange(free_list_, std::invoke(NextAccessor, free_list_));
                }
            }
            return coio::new_object<T>(alloc_);
        }

        COIO_ALWAYS_INLINE auto release(T& obj) noexcept -> void {
            std::scoped_lock _{lock_};
            std::invoke(NextAccessor, std::addressof(obj)) = std::exchange(free_list_, std::addressof(obj));
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_allocator() const noexcept -> allocator_type {
            return alloc_;
        }

    private:
        COIO_NO_UNIQUE_ADDRESS allocator_type alloc_;
        atomutex lock_;
        T* free_list_{nullptr};
    };
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
