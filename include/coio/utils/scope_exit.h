#pragma once
#include <utility>
#include "config.h"
#include "concepts.h"

namespace coio {

	template<typename EF>
		requires (std::is_destructible_v<EF> && std::is_object_v<EF>) ||
			(std::is_reference_v<EF> && std::invocable<std::add_lvalue_reference_t<EF>>)
	class scope_exit {
	public:
		template<typename Fn> requires (!std::is_same_v<std::remove_cvref_t<Fn>, scope_exit>) && std::is_constructible_v<EF, Fn> && (std::is_nothrow_constructible_v<EF, Fn> || std::is_nothrow_constructible_v<EF, Fn&>)
		explicit scope_exit(Fn&& fn) noexcept :
			on_exit(std::forward<std::conditional_t<!std::is_lvalue_reference_v<Fn> && std::is_nothrow_constructible_v<EF, Fn>, Fn, Fn&>>(fn)) {}

		template<typename Fn> requires (!std::is_same_v<std::remove_cvref_t<Fn>, scope_exit>) && std::is_constructible_v<EF, Fn> && (!std::is_nothrow_constructible_v<EF, Fn> && !std::is_nothrow_constructible_v<EF, Fn&>)
		explicit scope_exit(Fn&& fn) try :
			on_exit(fn) {}
		catch(...) {
			fn();
		}

		scope_exit(scope_exit&& other) noexcept(std::is_nothrow_move_constructible_v<EF> || std::is_nothrow_copy_constructible_v<EF>)
		requires std::is_nothrow_move_constructible_v<EF> || std::is_copy_constructible_v<EF> :
			on_exit(std::forward<std::conditional_t<std::is_nothrow_move_constructible_v<EF>, EF, EF&>>(other.on_exit)), flag(other.flag)
		{
			other.release();
		}

		scope_exit(const scope_exit&) = delete;

		~scope_exit() noexcept {
			if (flag) on_exit();
		}

		auto operator= (const scope_exit&) ->scope_exit& = delete;

		auto release() noexcept ->void {
			flag = false;
		}

	private:
		EF on_exit;
		bool flag = true;
	};

	template<typename EF>
	scope_exit(EF) -> scope_exit<EF>;

}