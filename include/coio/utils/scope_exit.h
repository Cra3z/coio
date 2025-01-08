#pragma once
#include <utility>
#include "../config.h"
#include "../concepts.h"

namespace coio {

    template<typename EF>
    class scope_exit {
        static_assert(
            (std::destructible<EF> and std::is_object_v<EF>) or std::is_lvalue_reference_v<EF>,
            "type `EF` shall be either: "
                "* destructible object-type, "
                "* lvalue reference to function or function object."
        );

        static_assert(
            std::invocable<std::add_lvalue_reference_t<std::remove_reference_t<EF>>>,
            "lvalue of `std::remove_reference_t<EF>` shall be invocable with no argument."
        );
    public:
        template<typename Fn> requires different_from<std::remove_cvref_t<Fn>, scope_exit> and std::constructible_from<EF, Fn> and (std::is_nothrow_constructible_v<EF, Fn> or std::is_nothrow_constructible_v<EF, Fn&>)
        constexpr explicit scope_exit(Fn&& fn) noexcept :
            on_exit(std::forward<std::conditional_t<!std::is_lvalue_reference_v<Fn> and std::is_nothrow_constructible_v<EF, Fn>, Fn, Fn&>>(fn)) {}

        template<typename Fn> requires different_from<std::remove_cvref_t<Fn>, scope_exit> and std::constructible_from<EF, Fn>
        constexpr explicit scope_exit(Fn&& fn) try :
            on_exit(fn) {}
        catch (...) {
            fn();
        }

        constexpr scope_exit(scope_exit&& other) noexcept(std::is_nothrow_move_constructible_v<EF> or std::is_nothrow_copy_constructible_v<EF>)
            requires std::is_nothrow_move_constructible_v<EF> or std::is_copy_constructible_v<EF> :
            on_exit(std::forward<std::conditional_t<std::is_nothrow_move_constructible_v<EF>, EF, EF&>>(other.on_exit)), flag(other.flag)
        {
            other.release();
        }

        scope_exit(const scope_exit&) = delete;

        constexpr ~scope_exit() noexcept {
            if (flag) on_exit();
        }

        auto operator= (const scope_exit&) ->scope_exit& = delete;

        constexpr auto release() noexcept ->void {
            flag = false;
        }

    private:
        COIO_NO_UNIQUE_ADDRESS EF on_exit;
        bool flag = true;
    };

    template<typename EF>
    scope_exit(EF) -> scope_exit<EF>;

}