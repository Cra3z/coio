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

        template<typename T, typename U>
        concept different_from_impl = not std::is_same_v<T, U>;

        template<typename T, template<typename...> typename Templ>
        struct template_spec_helper : std::false_type {};

        template<template<typename...> typename Tmpl, typename... Args>
        struct template_spec_helper<Tmpl<Args...>, Tmpl> : std::true_type {};

        template<typename T>
        using can_reference_helper = T&;

        template<typename T>
        concept can_reference_ = requires {
            typename can_reference_helper<T>;
        };

        template<typename T>
        concept boolean_testable = detail::boolean_testable_impl<T> and requires (T&& t) {
            { not static_cast<T&&>(t) } -> detail::boolean_testable_impl;
        };

        template<typename Awaiter, typename PromiseType>
        concept almost_awaiter_ = requires(Awaiter awaiter, std::coroutine_handle<PromiseType> coro) {
            { awaiter.await_ready() } -> boolean_testable;
            { awaiter.await_suspend(coro) } -> detail::valid_await_suspend_result;
            awaiter.await_resume();
        };

        template<typename Promise>
        concept almost_promise_ = std::is_class_v<Promise> and requires (Promise promise) {
            promise.get_return_object();
            promise.unhandled_exception();
            promise.initial_suspend();
            { promise.final_suspend() } noexcept;
        };

        struct get_awaiter_fn {
            template<typename Awaitable> requires requires { operator co_await(std::declval<Awaitable>()); }
            COIO_STATIC_CALL_OP decltype(auto) operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(operator co_await(std::declval<Awaitable>()))) {
                return operator co_await(std::forward<Awaitable>(awt));
            }

            template<typename Awaitable> requires requires { std::declval<Awaitable>().operator co_await(); }
            COIO_STATIC_CALL_OP decltype(auto) operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(std::declval<Awaitable>().operator co_await())) {
                return std::forward<Awaitable>(awt).operator co_await();
            }

            template<typename Awaitable>
            COIO_STATIC_CALL_OP decltype(auto) operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST noexcept {
                return std::forward<Awaitable>(awt);
            }

            // TODO: handle `promise_type::await_transform`
        };

        inline constexpr get_awaiter_fn get_awaiter{};

        template<typename Awaitable, typename Promise = void>
        struct awaitable_traits;

        template<typename Awaitable, typename Promise> requires requires {
            { get_awaiter(std::declval<Awaitable>()) } -> almost_awaiter_<Promise>;
        }
        struct awaitable_traits<Awaitable, Promise> {
            using awaiter_type = decltype(get_awaiter(std::declval<Awaitable>()));
            using result_type = decltype(std::declval<awaiter_type>().await_resume());
        };

        template<typename T, typename Promise = void>
        using await_result_t = typename awaitable_traits<T, Promise>::result_type;
    }

    using detail::get_awaiter;
    using detail::boolean_testable;

    template<typename Promise>
    concept simple_promise = detail::almost_promise_<Promise>;

    template<typename Promise>
    concept stoppable_promise = simple_promise<Promise> and requires (Promise promise) {
        { promise.unhandled_stop() } noexcept -> std::convertible_to<std::coroutine_handle<>>;
    };

    template<typename Awaiter, typename PromiseType = void>
    concept awaiter = (std::same_as<PromiseType, void> or simple_promise<PromiseType>) and detail::almost_awaiter_<Awaiter, PromiseType>;

    template<typename Awaitable, typename PromiseType = void>
    concept awaitable = awaiter<typename detail::awaitable_traits<Awaitable>::awaiter_type, PromiseType>;

    template<typename Awaitable, typename Promise = void>
    concept awaitable_value =
        std::move_constructible<std::remove_cvref_t<Awaitable>> and
        std::constructible_from<std::remove_cvref_t<Awaitable>, Awaitable> and
        awaitable<Awaitable, Promise>;

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
    concept unqualified_object = std::is_object_v<T> and std::same_as<std::remove_cv_t<T>, T>;

    template<typename T, template<typename...> typename Templ>
    concept specialization_of = detail::template_spec_helper<T, Templ>::value;

    template<typename R, typename T>
    concept container_compatible_range = std::ranges::input_range<R> and std::convertible_to<std::ranges::range_reference_t<R>, T> and std::constructible_from<T, std::ranges::range_reference_t<R>>;

    template<typename T>
    concept cpp17_iterator = std::copyable<T> and requires(T t) {
        {   *t } -> detail::can_reference_;
        {  ++t } -> std::same_as<T&>;
        { *t++ } -> detail::can_reference_;
    };

    template<typename T>
    concept cpp17_input_iterator = cpp17_iterator<T> and std::equality_comparable<T> && requires(T t) {
        typename std::incrementable_traits<T>::difference_type;
        typename std::indirectly_readable_traits<T>::value_type;
        typename std::common_reference_t<
            std::iter_reference_t<T>&&,
            typename std::indirectly_readable_traits<T>::value_type&
        >;
        *t++;
        typename std::common_reference_t<
            decltype(*t++)&&,
            typename std::indirectly_readable_traits<T>::value_type&
        >;
        requires std::signed_integral<typename std::incrementable_traits<T>::difference_type>;

    };

    template<typename T>
    concept cpp17_forward_iterator = cpp17_input_iterator<T> and
        std::constructible_from<T> and
        std::is_reference_v<std::iter_reference_t<T>> and
        std::same_as<
            std::remove_cvref_t<std::iter_reference_t<T>>,
            typename std::indirectly_readable_traits<T>::value_type
        > and requires(T t) {
        {  t++ } -> std::convertible_to<const T&>;
        { *t++ } -> std::same_as<std::iter_reference_t<T>>;
    };

    template<typename T>
    concept cpp17_bidirectional_iterator = cpp17_forward_iterator<T> and requires(T t) {
        {  --t } -> std::same_as<T&>;
        {  t-- } -> std::convertible_to<const T&>;
        { *t-- } -> std::same_as<std::iter_reference_t<T>>;
    };

    template<typename T>
    concept cpp17_random_access_iterator = cpp17_bidirectional_iterator<T> and std::totally_ordered<T> and requires(T t, typename std::incrementable_traits<T>::difference_type n) {
        { t += n } -> std::same_as<T&>;
        { t -= n } -> std::same_as<T&>;
        { t +  n } -> std::same_as<T>;
        { n +  t } -> std::same_as<T>;
        { t -  n } -> std::same_as<T>;
        { t -  t } -> std::same_as<decltype(n)>;
        {  t[n]  } -> std::convertible_to<std::iter_reference_t<T>>;
    };

    template<typename T, typename U>
    concept different_from = detail::different_from_impl<T, U> and detail::different_from_impl<U, T>;

    template<typename T>
    concept move_assignable = std::assignable_from<T&, T&&>;

    template<typename T>
    concept copy_assignable = move_assignable<T> and std::assignable_from<T&, const T&&> and std::assignable_from<T&, T&> and std::assignable_from<T&, const T&>;

    template<typename Comp, typename T, typename U>
    concept transparent_compare = requires { typename Comp::is_transparent; } and std::relation<Comp, const T&, const U&>;
}