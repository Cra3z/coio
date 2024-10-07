#pragma once
#include <type_traits>
#include <concepts>
#include <coroutine>

namespace coio {
	namespace detail {
		template<typename>
		struct is_coroutine_handle : std::false_type {};

		template<typename T>
		struct is_coroutine_handle<std::coroutine_handle<T>> : std::true_type {};

		template<typename T>
		concept valid_await_suspend_result = std::is_void_v<T> or std::same_as<T, bool> or is_coroutine_handle<T>::value;

		template<typename T>
		concept boolean_testable_impl = std::convertible_to<T, bool>;

		template<typename Awaitable>
		struct get_awaiter {
			using type = Awaitable;
		};

		template<typename Awaitable> requires requires { std::declval<Awaitable>().operator co_await(); }
		struct get_awaiter<Awaitable> {
			using type = decltype(std::declval<Awaitable>().operator co_await());
		};

		template<typename Awaitable> requires requires { operator co_await(std::declval<Awaitable>()); }
		struct get_awaiter<Awaitable> {
			using type = decltype(operator co_await(std::declval<Awaitable>()));
		};

	}

	template<typename T>
	concept boolean_testable = detail::boolean_testable_impl<T> and requires (T&& t) {
		{ not static_cast<T&&>(t) } -> detail::boolean_testable_impl;
	};

	template<typename T>
	concept promise = true;

	template<typename Awaiter, typename PromiseType>
	concept awaiter_for = requires(Awaiter awaiter, std::coroutine_handle<PromiseType> coro) {
		{ awaiter.await_ready() } -> boolean_testable;
		{ awaiter.await_suspend(coro) } -> detail::valid_await_suspend_result;
		awaiter.await_resume();
	};

	template<typename Awaiter>
	concept awaiter = awaiter_for<Awaiter, void>;

	template<typename Awaitable, typename PromiseType>
	concept awaitable_for = awaiter_for<typename detail::get_awaiter<Awaitable>::type, PromiseType>;

	template<typename Awaitable>
	concept awaitable = awaitable_for<Awaitable, void>;
}