#pragma once
#include <compare>
#include <utility>
#include <type_traits>
#include <functional>
#include <concepts>
#include "../config.h"

namespace coio {

	template<typename T>
	concept simple_retainable = requires (T* p) {
		p->retain();
		p->lose();
		requires noexcept(p->lose());
	};

    template<typename T>
    concept retainable = simple_retainable<T> and requires (T* p) {
        { p->use_count() } -> std::integral;
        requires noexcept(p->use_count());
    };

	template<typename T>
    class retain_ptr {
		static_assert(std::is_object_v<T> and not std::is_array_v<T> and std::destructible<T>, "type `T` shall be a complete non-array object-type.");
		static_assert(simple_retainable<T>, "type `T` shall meet the concept `coio::simple_retainable` at least.");
    public:

		using element_type = T;
        using pointer = T*;

    public:
        retain_ptr() = default;

        constexpr retain_ptr(std::nullptr_t) noexcept {} // NOLINT

        constexpr explicit retain_ptr(pointer ptr) noexcept(noexcept(ptr->retain())) : ptr_(ptr) {
            if (ptr_) ptr_->retain();
        }

        constexpr retain_ptr(const retain_ptr& other) : retain_ptr(other.ptr_) {}

        constexpr retain_ptr(retain_ptr&& other) noexcept : ptr_(std::exchange(other.ptr_, {})) {}

		template<typename U> requires std::derived_from<U, T> and std::convertible_to<U*, T*> and different_from<U, T>
		constexpr retain_ptr(retain_ptr<U> other) noexcept : ptr_(std::exchange(other.ptr_, {})) {}

        constexpr ~retain_ptr() {
            if (ptr_) ptr_->lose();
        }

        constexpr auto operator= (const retain_ptr& other) -> retain_ptr& {
            retain_ptr(other).swap(*this);
            return *this;
        }

        constexpr auto operator= (retain_ptr&& other) noexcept -> retain_ptr& {
            retain_ptr(std::move(other)).swap(*this);
            return *this;
        }

		template<typename U> requires std::derived_from<U, T> and std::convertible_to<U*, T*> and different_from<U, T>
		constexpr auto operator= (retain_ptr<U> other) noexcept ->retain_ptr& {
			retain_ptr(std::move(other)).swap(*this);
	        return *this;
        }

		constexpr friend auto operator== (const retain_ptr& lhs, std::nullptr_t) noexcept ->bool {
	        return lhs.ptr_ == nullptr;
        }

		constexpr friend auto operator<=> (const retain_ptr& lhs, std::nullptr_t) noexcept {
	        return std::compare_three_way{}(lhs.ptr_, nullptr);
        }

        constexpr friend auto operator== (const retain_ptr& lhs, const retain_ptr& rhs) noexcept ->bool = default;

        constexpr friend auto operator<=> (const retain_ptr& lhs, const retain_ptr& rhs) noexcept {
	        return std::compare_three_way{}(lhs.ptr_, rhs.ptr_);
        }

        constexpr explicit operator bool() const noexcept {
            return ptr_ != nullptr;
        }

        constexpr auto operator-> () const noexcept -> pointer {
            return ptr_;
        }

        constexpr auto operator* () const noexcept -> element_type& {
            COIO_ASSERT(ptr_ != nullptr);
            return *ptr_;
        }

        [[nodiscard]]
        constexpr auto get() const noexcept ->pointer {
            return ptr_;
        }

        [[nodiscard]]
        constexpr auto use_count() const noexcept requires retainable<T> {
            COIO_ASSERT(ptr_ != nullptr);
            return ptr_->use_count();
        }

		constexpr auto reset() noexcept ->void {
	        retain_ptr{}.swap(*this);
        }

		constexpr auto reset(pointer new_ptr) noexcept(noexcept(new_ptr->retain())) ->void {
	        retain_ptr(new_ptr).swap(*this);
        }

        constexpr auto swap(retain_ptr& other) noexcept ->void {
            std::swap(ptr_, other.ptr_);
        }

        constexpr friend auto swap(retain_ptr& lhs, retain_ptr& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

    private:
        pointer ptr_ = nullptr;
    };

    template<simple_retainable T, typename... Args> requires std::constructible_from<T, Args...>
    [[nodiscard]]
    constexpr auto make_retain(Args&&... args) ->retain_ptr<T> {
        return retain_ptr<T>(new T(std::forward<Args>(args)...));
    }
}

template<typename T>
struct std::hash<coio::retain_ptr<T>> : private std::hash<T*> {
	using std::hash<T*>::operator();
};