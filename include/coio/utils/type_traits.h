#pragma once
#include <type_traits>
#include <tuple>

namespace coio {
    template<typename... Types>
    struct type_list;

    namespace detail {
        template<std::size_t, typename T, typename TypeList>
        struct type_list_find_helper : std::integral_constant<std::size_t, std::size_t(-1)> {};

        template<std::size_t I, typename T, typename U, typename... Types>
        struct type_list_find_helper<I, T, type_list<U, Types...>> : type_list_find_helper<I+1, T, type_list<Types...>> {};

        template<std::size_t I, typename T, typename... Types>
        struct type_list_find_helper<I, T, type_list<T, Types...>> : std::integral_constant<std::size_t, I> {};

        template<std::size_t I, typename, typename... Rest>
        struct type_list_at_helper : type_list_at_helper<I-1, Rest...> {};

        template<typename First, typename... Rest>
        struct type_list_at_helper<0, First, Rest...> {
            using type = First;
        };

        template<typename TypeList1, typename TypeList2>
        struct type_list_concat2_helper;

        template<typename... TypeList1, typename... TypeList2>
        struct type_list_concat2_helper<type_list<TypeList1...>, type_list<TypeList2...>> {
            using type = type_list<TypeList1..., TypeList2...>;
        };

        template<typename... TypeLists>
        struct type_list_concat_helper;

        template<>
        struct type_list_concat_helper<> {
            using type = type_list<>;
        };

        template<typename... Types>
        struct type_list_concat_helper<type_list<Types...>> {
            using type = type_list<Types...>;
        };

        template<typename First, typename Second, typename... Rest>
        struct type_list_concat_helper<First, Second, Rest...> : type_list_concat_helper<typename type_list_concat2_helper<First, Second>::type, Rest...> {};

        template<typename First, typename... Rest>
        struct type_list_pop_front_helper {
            using type = type_list<Rest...>;
        };

        template<typename TypeList, typename...>
        struct type_list_pop_back_helper;

        template<typename... Types, typename First, typename... Rest>
        struct type_list_pop_back_helper<type_list<Types...>, First, Rest...> : type_list_pop_back_helper<type_list<Types..., First>, Rest...> {};

        template<typename... Types, typename T>
        struct type_list_pop_back_helper<type_list<Types...>, T> {
            using type = type_list<Types...>;
        };

        template<typename TypeList, typename...>
        struct type_list_reverse_helper {
            using type = TypeList;
        };

        template<typename... Types, typename First, typename... Rest>
        struct type_list_reverse_helper<type_list<Types...>, First, Rest...> : type_list_reverse_helper<type_list<First, Types...>, Rest...> {};

        template<typename T, typename U>
        struct type_list_replace_helper {
            template<typename V>
            using type = std::conditional_t<std::is_same_v<V, T>, U, V>;
        };

        template<typename OutTypeList, typename TypeList, typename T>
        struct type_list_remove_helper;

        template<typename... OutTypes, typename First, typename... Rest, typename T>
        struct type_list_remove_helper<type_list<OutTypes...>, type_list<First, Rest...>, T> : type_list_remove_helper<type_list<OutTypes..., First>, type_list<Rest...>, T> {};

        template<typename... OutTypes, typename First, typename... Rest>
        struct type_list_remove_helper<type_list<OutTypes...>, type_list<First, Rest...>, First> : type_list_remove_helper<type_list<OutTypes...>, type_list<Rest...>, First> {};

        template<typename... OutTypes, typename T>
        struct type_list_remove_helper<type_list<OutTypes...>, type_list<>, T> {
            using type = type_list<OutTypes...>;
        };

        template<typename OutTypeList, typename TypeList>
        struct type_list_unique_helper {
            using type = OutTypeList;
        };

        template<typename... OutTypes, typename First, typename... Rest>
        struct type_list_unique_helper<type_list<OutTypes...>, type_list<First, Rest...>> : type_list_unique_helper<type_list<OutTypes..., First>, typename type_list_remove_helper<type_list<>, type_list<First, Rest...>, First>::type> {};
    }

    template<typename... Types>
    struct type_list {
        static constexpr std::size_t size = sizeof...(Types);

        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        template<typename T>
        static constexpr std::size_t find = detail::type_list_find_helper<0, T, type_list>::value;

        template<typename T>
        static constexpr bool contains = find<T> != npos;

        template<std::size_t I>
        using at = typename detail::type_list_at_helper<I, Types...>::type;

        template<typename... TypeLists>
        using concat = typename detail::type_list_concat_helper<type_list, TypeLists...>::type;

        template<typename T>
        using push_front = type_list<T, Types...>;

        template<typename T>
        using push_back = type_list<Types..., T>;

        template<typename... NewTypes>
        using prepend = type_list<NewTypes..., Types...>;

        template<typename... NewTypes>
        using append = type_list<Types..., NewTypes...>;

        using pop_front = typename detail::type_list_pop_front_helper<Types...>::type;

        using pop_back = typename detail::type_list_pop_back_helper<type_list<>, Types...>::type;

        using reverse = typename detail::type_list_reverse_helper<type_list<>, Types...>::type;

        template<typename T>
        using remove = typename detail::type_list_remove_helper<type_list<>, type_list, T>::type;

        template<template<typename...> typename F>
        using transform = type_list<F<Types>...>;

        template<typename T, typename U>
        using replace = transform<detail::type_list_replace_helper<T, U>::template type>;

        template<template<typename...> typename F>
        using apply = F<Types...>;

        using unique = typename detail::type_list_unique_helper<type_list<>, type_list>::type;

    };

    template<>
    struct type_list<> {
        static constexpr std::size_t size = 0;

        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        template<typename T>
        static constexpr std::size_t find = npos;

        template<typename T>
        static constexpr bool containes = false;

        template<typename... TypeLists>
        using concat = typename detail::type_list_concat_helper<type_list, TypeLists...>::type;

        template<typename T>
        using push_front = type_list<T>;

        template<typename T>
        using push_back = type_list<T>;

        template<typename... NewTypes>
        using prepend = type_list<NewTypes...>;

        template<typename... NewTypes>
        using append = type_list<NewTypes...>;

        using reverse = type_list;

        template<typename T>
        using remove = type_list;

        template<template<typename...> typename F>
        using transform = type_list<F<>>;

        template<typename T, typename U>
        using replace = type_list;

        template<template<typename...> typename F>
        using apply = F<>;

        using unique = type_list;

    };

    template<typename T>
    using wrap_ref_t = std::conditional_t<std::is_lvalue_reference_v<T>, std::reference_wrapper<std::remove_reference_t<T>>, T>;

    template<typename T>
    using add_const_lvalue_ref_t = std::add_lvalue_reference_t<std::add_const_t<T>>;

    template<bool cond, typename T>
    using add_const_if_t = std::conditional_t<cond, std::add_const_t<T>, T>;

    template<typename T, typename U>
    using const_like_t = add_const_if_t<std::is_const_v<U>, T>;

}