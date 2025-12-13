#pragma once
#include "config.h"
#include "../concepts.h"
#include "../utils/type_traits.h"

namespace coio {
    struct nothing {};

    namespace detail {
        class optional_void {
        public:
            using value_type = void;

        public:
            constexpr optional_void() = default;

            constexpr optional_void(std::nullopt_t) noexcept {}

            constexpr explicit optional_void(std::in_place_t) noexcept : has_value_(true) {}

            constexpr optional_void(const optional_void&) = default;

            constexpr optional_void(optional_void&& other) noexcept : has_value_(std::exchange(other.has_value_, false)) {}

            constexpr auto operator= (const optional_void&) noexcept -> optional_void& = default;

            constexpr auto operator= (optional_void&& other) noexcept -> optional_void& {
                has_value_ = std::exchange(other.has_value_, false);
                return *this;
            }

            constexpr auto operator= (std::nullopt_t) noexcept -> optional_void& {
                has_value_ = false;
                return *this;
            }

            constexpr auto reset() noexcept -> void {
                has_value_ = false;
            }

            constexpr auto emplace() noexcept -> void {
                has_value_ = true;
            }

            constexpr auto value() const -> void {
                if (not has_value_) throw std::bad_optional_access{};
            }

            constexpr auto operator*() const noexcept -> void {}

            [[nodiscard]]
            constexpr auto has_value() const noexcept -> bool {
                return has_value_;
            }

            constexpr explicit operator bool() const noexcept {
                return has_value_;
            }

            constexpr friend auto operator== (const optional_void& lhs, const optional_void& rhs) noexcept -> bool {
                return lhs.has_value_ == rhs.has_value_;
            }

            constexpr friend auto operator<=> (const optional_void& lhs, const optional_void& rhs) noexcept {
                return int(lhs.has_value_) <=> int(rhs.has_value_);
            }

        private:
            bool has_value_ = false;
        };

        template<typename T>
        class optional_ref {
        public:
            using value_type = T&;

        public:
            constexpr optional_ref() = default;

            constexpr optional_ref(std::nullopt_t) noexcept {}

            constexpr explicit optional_ref(std::in_place_t, T& ref) noexcept : value_(std::addressof(ref)) {}

            constexpr optional_ref(const optional_ref&) = default;

            constexpr optional_ref(optional_ref&& other) noexcept : value_(std::exchange(other.value_, false)) {}

            constexpr auto operator= (const optional_ref&) noexcept -> optional_ref& = default;

            constexpr auto operator= (optional_ref&& other) noexcept -> optional_ref& {
                value_ = std::exchange(other.value_, false);
                return *this;
            }

            constexpr auto operator= (std::nullopt_t) noexcept -> optional_ref& {
                value_ = nullptr;
                return *this;
            }

            constexpr auto reset() noexcept -> void {
                value_ = nullptr;
            }

            constexpr auto emplace(T& ref) noexcept -> T& {
                value_ = std::addressof(ref);
                return *value_;
            }

            constexpr auto value() const -> T& {
                if (not value_) throw std::bad_optional_access{};
                return *value_;
            }

            constexpr auto operator*() const noexcept -> T& {
                return *value_;
            }

            constexpr auto operator->() const noexcept -> T* {
                return value_;
            }

            [[nodiscard]]
            constexpr auto has_value() const noexcept -> bool {
                return value_ != nullptr;
            }

            constexpr explicit operator bool() const noexcept {
                return value_ != nullptr;
            }

            constexpr friend auto operator== (const optional_ref& lhs, const optional_ref& rhs) noexcept -> bool {
                return lhs.value_ == rhs.value_;
            }

            constexpr friend auto operator<=> (const optional_ref& lhs, const optional_ref& rhs) noexcept {
                return lhs.value_ <=> rhs.value_;
            }

        private:
            T* value_ = nullptr;
        };

        template<typename T>
        struct add_optional {
            using type = std::optional<T>;
        };

        template<typename T>
        struct add_optional<T&> {
            using type = optional_ref<T>;
        };

        template<typename Void> requires std::is_void_v<Void>
        struct add_optional<Void> {
            using type = optional_void;
        };

        template<typename T>
        using optional_t = typename add_optional<T>::type;

        template<typename T>
        class promise_return_control {
        public:
            auto return_value(T value) noexcept(std::is_nothrow_constructible_v<wrap_ref_t<T>, T>) -> void {
                result_.template emplace<1>(static_cast<T&&>(value));
            }

            auto unhandled_exception() noexcept -> void {
                result_.template emplace<2>(std::current_exception());
            }

            auto get_result() -> T& {
                COIO_ASSERT(result_.index() > 0);
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
                return *std::get_if<1>(&result_);
            }

            auto try_get_result() -> T* {
                if (result_.index() == 0) return nullptr;
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
                return std::get_if<1>(&result_);
            }

            decltype(auto) get_non_void_result() {
                return get_result();
            }

        private:
            std::variant<std::monostate, wrap_ref_t<T>, std::exception_ptr> result_;
        };

        template<>
        class promise_return_control<void> {
        public:
            auto return_void() noexcept -> void {
                result_.emplace<1>();
            }

            auto unhandled_exception() noexcept -> void {
                result_.emplace<2>(std::current_exception());
            }

            auto get_result() -> void {
                COIO_ASSERT(result_.index() > 0);
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
            }

            auto try_get_result() -> optional_t<void> {
                if (result_.index() == 0) return {};
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
                return optional_t<void>{std::in_place};
            }

            auto get_non_void_result() -> nothing {
                get_result();
                return nothing{};
            }

        private:
            std::variant<std::monostate, nothing, std::exception_ptr> result_;
        };
    }
}