#pragma once
#include <cstring>
#include <memory>
#include <utility>
#include "../config.h"

namespace coio::detail {

    struct default_align_t {
        alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) std::byte _[__STDCPP_DEFAULT_NEW_ALIGNMENT__];
    };

    COIO_ALWAYS_INLINE constexpr auto ceiling_division(std::size_t n, std::size_t m) noexcept ->std::size_t {
        COIO_ASSERT(m != 0);
        return (n + m - 1) / m;
    }

    COIO_ALWAYS_INLINE constexpr auto ceiling_align(std::size_t n, std::size_t m) noexcept ->std::size_t {
        COIO_ASSERT(m != 0);
        return ceiling_division(n, m) * m;
    }

    inline auto align_address(void* ptr, std::size_t alignment, std::size_t least_offset) noexcept ->void* {
        auto least = reinterpret_cast<std::uintptr_t>(static_cast<std::byte*>(ptr) + least_offset);
        return reinterpret_cast<void*>(std::uintptr_t(ceiling_align(least, alignment)));
    };

    using dealloc_fn_t = void(*)(default_align_t*, std::size_t) noexcept; ///< (first address of default_align_t-array, size of coroutine-state)

    template<typename Alloc>
    struct co_memory {

        static_assert(requires {typename std::allocator_traits<Alloc>::template rebind_alloc<default_align_t>;}, "can't rebind `Alloc` to `default_align_t`.");

        using alloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<default_align_t>;

        using alloc_traits = std::allocator_traits<alloc_t>;

        static constexpr std::size_t alloc_max_alignment = std::max(alignof(alloc_t), alignof(default_align_t));

        static auto allocate(Alloc alloc_, std::size_t n) ->void* {
            alloc_t allocator_(std::move(alloc_));
            if constexpr (std::default_initializable<Alloc> and alloc_traits::is_always_equal::value) { // for stateless-allocator
                /*
                 * layout:
                 *   +-----------------+------------------+
                 *   | coroutine state | possible padding |
                 *   +-----------------+------------------+
                 *           |
                 *      `n` bytes
                 */
                return alloc_traits::allocate(allocator_, ceiling_division(n, sizeof(default_align_t)));
            }
            else {
                /*
                 * layout:
                 *   +-----------------+------------------+-----------+------------------+
                 *   | coroutine state | possible padding | allocator | possible padding |
                 *   +-----------------+------------------+-----------+------------------+
                 *           |                                  |
                 *      `n` bytes                   `sizeof(Allocator)` bytes
                 */
                void* result = alloc_traits::allocate(allocator_, (n + sizeof(alloc_t) + alloc_max_alignment - 1) / alignof(default_align_t));
                void(std::construct_at(static_cast<alloc_t*>(align_address(result, alignof(alloc_t), n)), std::move(allocator_)));
                return result;
            }
        }

        static auto deallocate(void* ptr, std::size_t n) noexcept ->void {
            if constexpr (std::default_initializable<Alloc> and alloc_traits::is_always_equal::value) { // for stateless-allocator
                alloc_t allocator_(Alloc{});
                alloc_traits::deallocate(allocator_, static_cast<default_align_t*>(ptr), ceiling_division(n, alignof(default_align_t)));
            }
            else {
                auto alloc_ptr_ = std::launder(static_cast<alloc_t*>(align_address(ptr, alignof(alloc_t), n)));
                alloc_t allocator_ = std::move(*alloc_ptr_);
                std::destroy_at(alloc_ptr_);
                alloc_traits::deallocate(allocator_, static_cast<default_align_t*>(ptr), (n + sizeof(alloc_t) + alloc_max_alignment - 1) / alignof(default_align_t));
            }
        }

    };

    template<>
    struct co_memory<void> {
        template<typename Alloc>
        static auto allocate(Alloc alloc_, std::size_t n) ->void* {
            using alloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<default_align_t>;
            using alloc_traits = std::allocator_traits<alloc_t>;
            alloc_t allocator_(std::move(alloc_));
            if constexpr (std::default_initializable<Alloc> and alloc_traits::is_always_equal::value) { // for stateless-allocator
                /*
                 * layout:
                 *   +-----------------+-----------------------+------------------+
                 *   | coroutine state | deallocating function | possible padding |
                 *   +-----------------+-----------------------+------------------+
                 *           |                     |
                 *      `n` bytes     `sizeof(dealloc_fn_t)` bytes
                 */
                auto offset_of_dealloc_fn = n;
                n += sizeof(dealloc_fn_t);
                void* result = alloc_traits::allocate(allocator_, ceiling_division(n, sizeof(default_align_t)));
                dealloc_fn_t dealloc_fn = +[](default_align_t* ptr, std::size_t co_state_size) noexcept {
                    auto length = co_state_size + sizeof(dealloc_fn_t);
                    alloc_t allocator_(Alloc{});
                    alloc_traits::deallocate(allocator_, ptr, ceiling_division(length, alignof(default_align_t)));
                };
                std::memcpy(static_cast<std::byte*>(result) + offset_of_dealloc_fn, &dealloc_fn, sizeof(dealloc_fn_t));
                return result;
            }
            else {
                 /*
                 * layout:
                 *   +-----------------+-----------------------+------------------+-----------+------------------+
                 *   | coroutine state | deallocating function | possible padding | allocator | possible padding |
                 *   +-----------------+-----------------------+------------------+-----------+------------------+
                 *           |                     |                                    |
                 *      `n` bytes     `sizeof(dealloc_fn_t)` bytes          `sizeof(Allocator)` bytes
                 */
                static constexpr std::size_t max_alignment = std::max(alignof(alloc_t), alignof(default_align_t));
                auto offset_of_dealloc_fn = n;
                n += sizeof(dealloc_fn_t);
                void* result = alloc_traits::allocate(allocator_, (n + sizeof(alloc_t) + max_alignment - 1) / alignof(default_align_t));
                dealloc_fn_t dealloc_fn = +[](default_align_t* ptr, std::size_t co_state_size) noexcept {
                    auto length = co_state_size + sizeof(dealloc_fn_t);
                    auto alloc_ptr_ = std::launder(static_cast<alloc_t*>(align_address(ptr, alignof(alloc_t), length)));
                    alloc_t allocator_ = std::move(*alloc_ptr_);
                    std::destroy_at(alloc_ptr_);
                    alloc_traits::deallocate(allocator_, ptr, (length + sizeof(alloc_t) + max_alignment - 1) / alignof(default_align_t));
                };
                std::memcpy(static_cast<std::byte*>(result) + offset_of_dealloc_fn, &dealloc_fn, sizeof(dealloc_fn_t));
                void(std::construct_at(static_cast<alloc_t*>(align_address(result, alignof(alloc_t), n)), std::move(allocator_)));
                return result;
            }
        }

        static auto deallocate(void* ptr, std::size_t n) noexcept ->void {
            dealloc_fn_t dealloc_fn = nullptr;
            std::memcpy(&dealloc_fn, static_cast<std::byte*>(ptr) + n, sizeof(dealloc_fn_t));
            dealloc_fn(static_cast<default_align_t*>(ptr), n);
        }
    };

    template<typename Alloc>
    concept valid_coroutine_alloctor_ = std::same_as<Alloc, void> or (std::is_pointer_v<typename std::allocator_traits<Alloc>::pointer> and std::is_object_v<typename std::allocator_traits<Alloc>::template rebind_alloc<default_align_t>>);

    template<typename Alloc>
    struct promise_alloc_control {
        auto operator new (std::size_t n) ->void* requires std::same_as<Alloc, void> or std::default_initializable<Alloc> {
            return co_memory<Alloc>::allocate(std::conditional_t<std::same_as<Alloc, void>, std::allocator<void>, Alloc>(), n);
        }

        template<typename OtherAlloc, typename... Args> requires std::same_as<Alloc, void> or std::convertible_to<const OtherAlloc&, Alloc>
        auto operator new (std::size_t n, std::allocator_arg_t, const OtherAlloc& other_alloc, const Args&...) ->void* { // for normal corotuine function `auto some_function(std::allocator_arg_t, allocator, ...) ->coio::task<...>`
            return co_memory<Alloc>::allocate(other_alloc, n);
        }

        template<typename This, typename OtherAlloc, typename... Args> requires std::same_as<Alloc, void> or std::convertible_to<const OtherAlloc&, Alloc>
        auto operator new (std::size_t n, const This&, std::allocator_arg_t, const OtherAlloc& other_alloc, const Args&...) ->void* { // for non-static member corotuine function `auto some_class::some_function(std::allocator_arg_t, allocator, ...) ->coio::task<...>`
            return operator new (n, std::allocator_arg, other_alloc);
        }

        auto operator delete (void* ptr, std::size_t n) noexcept ->void {
            co_memory<Alloc>::deallocate(ptr, n);
        }
    };
}