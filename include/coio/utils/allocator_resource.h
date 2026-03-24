// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <memory>
#include <memory_resource>
#include <cstddef>
#include "utility.h"
#include "../detail/concepts.h"

namespace coio {
    class allocator_resource : public std::pmr::memory_resource {
    private:
        template<std::size_t N>
        struct placeholder {
            alignas(N) std::byte _[N];
        };

        template<typename T>
        static constexpr bool is_small_object = sizeof(T) <= sizeof(void*) and alignof(T) <= alignof(void*);

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        struct proxied_base {
            virtual auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* = 0;

            virtual auto do_deallocate(void* p, std::size_t bytes, std::size_t alignment) -> void = 0;

            virtual auto do_is_equal(const proxied_base& other) const noexcept -> bool = 0;

            virtual auto destroy() noexcept -> void = 0;
        };

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<typename Alloc>
        struct proxied : proxied_base {
            explicit proxied(Alloc alloc) noexcept : alloc_(std::move(alloc)) {}

            proxied(const proxied&) = delete;

            proxied& operator= (const proxied&) = delete;

            template<std::size_t N>
            COIO_ALWAYS_INLINE auto alloc_impl(std::size_t bytes) -> void* {
                using alloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<placeholder<N>>;
                using traits_t = std::allocator_traits<alloc_t>;
                alloc_t alloc = alloc_;
                std::size_t count = bytes == 0 ? 1 : (bytes + N - 1) / N;
                return traits_t::allocate(alloc, count);
            }

            template<std::size_t N>
            COIO_ALWAYS_INLINE auto dealloc_impl(void* p, std::size_t bytes) -> void {
                using alloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<placeholder<N>>;
                using traits_t = std::allocator_traits<alloc_t>;
                alloc_t alloc(alloc_);
                std::size_t count = bytes == 0 ? 1 : (bytes + N - 1) / N;
                traits_t::deallocate(alloc, static_cast<placeholder<N>*>(p), count);
            }

            auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
                switch (alignment) {
                    case 1:   return alloc_impl<1>(bytes);
                    case 2:   return alloc_impl<2>(bytes);
                    case 4:   return alloc_impl<4>(bytes);
                    case 8:   return alloc_impl<8>(bytes);
                    case 16:  return alloc_impl<16>(bytes);
                    case 32:  return alloc_impl<32>(bytes);
                    case 64:  return alloc_impl<64>(bytes);
                    case 128: return alloc_impl<128>(bytes);
                    case 256: return alloc_impl<256>(bytes);
                    default: [[unlikely]] throw std::bad_alloc{};
                }
            }

            auto do_deallocate(void* p, std::size_t bytes, std::size_t alignment) -> void override {
                if (p == nullptr) return;
                switch (alignment) {
                    case 1:   return dealloc_impl<1>(p, bytes);
                    case 2:   return dealloc_impl<2>(p, bytes);
                    case 4:   return dealloc_impl<4>(p, bytes);
                    case 8:   return dealloc_impl<8>(p, bytes);
                    case 16:  return dealloc_impl<16>(p, bytes);
                    case 32:  return dealloc_impl<32>(p, bytes);
                    case 64:  return dealloc_impl<64>(p, bytes);
                    case 128: return dealloc_impl<128>(p, bytes);
                    case 256: return dealloc_impl<256>(p, bytes);
                    default: [[unlikely]] unreachable();
                }
            }

            auto do_is_equal(const proxied_base& other) const noexcept -> bool override {
                if (auto other_ = dynamic_cast<const proxied*>(&other)) {
                    return alloc_ == other_->alloc_;
                }
                return false;
            }

            auto destroy() noexcept -> void override {
                if constexpr (is_small_object<Alloc>) { // this object allocated at `allocator_resource::storage_`
                    std::destroy_at(this);
                }
                else {
                    using alloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<proxied>;
                    using traits_t = std::allocator_traits<alloc_t>;
                    alloc_t alloc(alloc_);
                    traits_t::destroy(alloc, this);
                    traits_t::deallocate(alloc, this, 1);
                }
            }

            COIO_NO_UNIQUE_ADDRESS Alloc alloc_;
        };

    public:
        // ReSharper disable once CppPossiblyUninitializedMember
        template<simple_allocator Alloc>
        explicit allocator_resource(Alloc alloc) { // NOLINT(*-pro-type-member-init)
            proxied<Alloc>* location = nullptr;
            if constexpr (is_small_object<Alloc>) {
                location = std::construct_at(reinterpret_cast<proxied<Alloc>*>(storage_), alloc);
            }
            else {
                using alloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<proxied<Alloc>>;
                using traits_t = std::allocator_traits<alloc_t>;
                alloc_t pro_alloc(alloc);
                location = traits_t::allocate(pro_alloc, 1);
                try {
                    traits_t::construct(pro_alloc, location, alloc);
                }
                catch (...) {
                    traits_t::deallocate(pro_alloc, location, 1);
                    throw;
                }
            }
            impl_ = location;
        }

        allocator_resource(const allocator_resource&) = delete;

        ~allocator_resource() override {
            impl_->destroy();
        }

        auto operator= (const allocator_resource&) -> allocator_resource& = delete;

    private:
        COIO_ALWAYS_INLINE auto do_allocate(std::size_t bytes, std::size_t alignment) -> void* override {
            return impl_->do_allocate(bytes, alignment);
        }

        COIO_ALWAYS_INLINE auto do_deallocate(void* p, std::size_t bytes, std::size_t alignment) -> void override {
            impl_->do_deallocate(p, bytes, alignment);
        }

        COIO_ALWAYS_INLINE auto do_is_equal(const std::pmr::memory_resource& other) const noexcept -> bool override {
            if (auto other_ = dynamic_cast<const allocator_resource*>(&other)) {
                return impl_->do_is_equal(*other_->impl_);
            }
            return false;
        }

    private:
        proxied_base* impl_;
        alignas(void*) std::byte storage_[sizeof(void*)];
    };


    template<typename Alloc>
    struct allocator_adaptor {
        static_assert(simple_allocator<Alloc>);

        COIO_ALWAYS_INLINE auto get_allocator() const noexcept {
            using allocator_type = std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
            return allocator_type(alloc);
        }

        COIO_NO_UNIQUE_ADDRESS Alloc alloc;
    };

    template<>
    struct allocator_adaptor<void> {
        allocator_adaptor() noexcept : resource(std::allocator<std::byte>{}) {}

        explicit allocator_adaptor(simple_allocator auto alloc) noexcept : resource(std::move(alloc)) {}

        COIO_ALWAYS_INLINE auto get_allocator() const noexcept -> std::pmr::polymorphic_allocator<> {
            return &resource;
        }

        mutable allocator_resource resource;
    };
}