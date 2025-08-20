#pragma once
#include "../config.h"
#include "../concepts.h"
#include "../utils/type_traits.h"

namespace coio {
    struct nothing {};

    namespace detail {
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

        private:
            std::variant<std::monostate, nothing, std::exception_ptr> result_;
        };
    }

    struct fire_and_forget {
        struct promise_type {
            static auto get_return_object() noexcept -> fire_and_forget {
                return {};
            }

            static auto initial_suspend() noexcept -> std::suspend_never {
                return {};
            }

            static auto final_suspend() noexcept -> std::suspend_never {
                return {};
            }

            static auto return_void() noexcept -> void {}

            [[noreturn]]
            static auto unhandled_exception() noexcept -> void {
                std::terminate();
            }
        };
    };
}