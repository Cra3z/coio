#pragma once
#include <type_traits>
#include <concepts>
#include <coroutine>
#include <ranges>

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

		template<typename T, template<typename...> typename Templ>
		struct template_spec_helper : std::false_type {};

		template<template<typename...> typename Tmpl, typename... Args>
		struct template_spec_helper<Tmpl<Args...>, Tmpl> : std::true_type {};
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

	template<typename Range>
	concept elements_move_insertable_range = std::ranges::forward_range<Range> and std::move_constructible<std::ranges::range_value_t<Range>> and requires (Range range, std::ranges::iterator_t<Range> it) {
		range.insert(it, std::declval<std::ranges::range_value_t<Range>>());
	};

	template<typename Range>
	concept elements_copy_insertable_range = elements_move_insertable_range<Range> and std::copy_constructible<std::ranges::range_value_t<Range>> and requires (Range range, std::ranges::iterator_t<Range> it, std::ranges::range_value_t<Range> value) {
		range.insert(it, value);
	};

	template<typename T>
	concept borrowed_forward_range = std::ranges::forward_range<T> and std::ranges::borrowed_range<T>;

	template<typename T>
	concept value_type = std::is_object_v<T> and std::same_as<std::remove_cv_t<T>, T>;

	template<typename T, template<typename...> typename Templ>
	concept specialization_of = detail::template_spec_helper<T, Templ>::value;
}