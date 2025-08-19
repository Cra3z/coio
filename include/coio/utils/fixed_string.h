#pragma once
#include <ostream>
#include <concepts>
#include <cstring>
#include <iterator>
#include <string_view>
#include <typeindex>
#include "../config.h"
#include "format.h"

namespace coio {
    template<typename CharType, std::size_t N>
    class basic_fixed_string {
        static_assert(std::same_as<std::remove_cvref_t<CharType>, CharType>);
        static_assert(not std::is_array_v<CharType> and std::is_trivial_v<CharType> and std::is_standard_layout_v<CharType>);
    public:
        using value_type = CharType;
        using reference = CharType&;
        using const_reference = const CharType&;
        using pointer = CharType*;
        using const_pointer = const CharType*;
        using iterator = pointer;
        using const_iterator = const_pointer;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

    public:
        constexpr basic_fixed_string(const CharType* ptr, std::integral_constant<std::size_t, N>) noexcept {
            if (std::is_constant_evaluated()) {
                for (std::size_t i = 0; i < N; ++i) internal_[i] = ptr[i];
            }
            else std::memmove(internal_, ptr, N * sizeof(CharType));
        }

        constexpr basic_fixed_string(const CharType(&char_array)[N + 1]) noexcept :
            basic_fixed_string(char_array, std::integral_constant<std::size_t, N>{}) {}

        template<std::convertible_to<CharType>... RestCharType> requires(sizeof...(RestCharType) + 1 == N)
        constexpr explicit basic_fixed_string(CharType first, RestCharType... rest) noexcept : internal_{first, static_cast<CharType>(rest)...} {}

        [[nodiscard]]
        static constexpr auto empty() noexcept -> bool {
          return N == 0;
        }

        [[nodiscard]]
        static constexpr auto size() noexcept -> size_type {
            return N;
        }

        [[nodiscard]]
        static constexpr auto length() noexcept -> size_type {
            return size();
        }

        [[nodiscard]]
        static constexpr auto capacity() noexcept -> size_type {
            return size();
        }

        [[nodiscard]]
        static constexpr auto max_size() noexcept -> size_type {
            return size();
        }

        [[nodiscard]]
        constexpr auto data() noexcept -> pointer {
            return internal_;
        }

        [[nodiscard]]
        constexpr auto data() const noexcept -> const_pointer {
            return internal_;
        }

        [[nodiscard]]
        constexpr auto c_str() const noexcept -> const_pointer {
            return data();
        }

        [[nodiscard]]
        constexpr auto operator[](size_type index) noexcept -> reference {
            COIO_ASSERT(index < N);
            return internal_[index];
        }

        [[nodiscard]]
        constexpr auto operator[](size_type index) const noexcept -> const_reference {
            COIO_ASSERT(index < N);
            return internal_[index];
        }

        [[nodiscard]]
        constexpr auto front() noexcept -> reference {
            COIO_ASSERT(not empty());
            return internal_[0];
        }

        [[nodiscard]]
        constexpr auto front() const noexcept -> const_reference {
            COIO_ASSERT(not empty());
            return internal_[0];
        }

        [[nodiscard]]
        constexpr auto back() noexcept -> reference {
            COIO_ASSERT(not empty());
            return internal_[size() - 1];
        }

        [[nodiscard]]
        constexpr auto back() const noexcept -> const_reference {
            COIO_ASSERT(not empty());
            return internal_[size() - 1];
        }

        [[nodiscard]]
        constexpr auto begin() noexcept -> iterator {
            return internal_;
        }

        [[nodiscard]]
        constexpr auto begin() const noexcept -> const_iterator {
            return internal_;
        }

        [[nodiscard]]
        constexpr auto cbegin() const noexcept -> const_iterator {
            return begin();
        }

        [[nodiscard]]
        constexpr auto rbegin() noexcept -> reverse_iterator {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]]
        constexpr auto rbegin() const noexcept -> const_reverse_iterator {
            return std::make_reverse_iterator(end());
        }

        [[nodiscard]]
        constexpr auto crbegin() const noexcept -> const_reverse_iterator {
            return rbegin();
        }

        [[nodiscard]]
        constexpr auto end() noexcept -> iterator {
            return internal_ + N;
        }

        [[nodiscard]]
        constexpr auto end() const noexcept -> const_iterator {
            return internal_ + N;
        }

        [[nodiscard]]
        constexpr auto cend() const noexcept -> const_iterator {
            return end();
        }

        [[nodiscard]]
        constexpr auto rend() noexcept -> reverse_iterator {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]]
        constexpr auto rend() const noexcept -> const_reverse_iterator {
            return std::make_reverse_iterator(begin());
        }

        [[nodiscard]]
        constexpr auto crend() const noexcept -> const_reverse_iterator {
            return rend();
        }

        [[nodiscard]]
        constexpr auto view() const noexcept -> std::basic_string_view<CharType> {
            return std::basic_string_view<CharType>(internal_, N);
        }

        template<std::size_t M>
        [[nodiscard]]
        constexpr friend auto operator+ (
            const basic_fixed_string& lhs, const basic_fixed_string<value_type, M>& rhs
        ) noexcept -> basic_fixed_string<value_type, N + M> {
            return [&lhs, &rhs]<std::size_t... I, std::size_t... J>(std::index_sequence<I...>, std::index_sequence<J...>) noexcept {
                return basic_fixed_string<value_type, N + M>{lhs[I]..., rhs[J]...};
            }(std::make_index_sequence<N>{}, std::make_index_sequence<M>{});
        }

        [[nodiscard]]
        constexpr auto operator== (const basic_fixed_string& other) const noexcept -> bool {
            return view() == other.view();
        }

        template<std::size_t N2>
        [[nodiscard]]
        friend constexpr auto operator== (const basic_fixed_string& lhs, const basic_fixed_string<CharType, N2>& rhs) noexcept -> bool {
            return lhs.view() == rhs.view();
        }

        template<std::size_t N2>
        [[nodiscard]]
        friend constexpr auto operator<=>(const basic_fixed_string& lhs, const basic_fixed_string<CharType, N2>& rhs) noexcept {
            return lhs.view() <=> rhs.view();
        }

        template<typename Traits>
        friend auto operator<< (std::basic_ostream<CharType, Traits>& os, const basic_fixed_string& str) -> std::basic_ostream<CharType, Traits>& {
            return os << str.view();
        }

    public:
        CharType internal_[N + 1]{};
    };

    template<typename CharType, std::size_t N>
    basic_fixed_string(const CharType(&str)[N]) -> basic_fixed_string<CharType, N - 1>;

    template<typename CharType, std::size_t N>
    basic_fixed_string(const CharType* ptr, std::integral_constant<std::size_t, N>) -> basic_fixed_string<CharType, N>;

    template<typename CharType, std::convertible_to<CharType>... Rest>
    basic_fixed_string(CharType, Rest...) -> basic_fixed_string<CharType, sizeof...(Rest) + 1>;

    template<std::size_t N>
    using fixed_string = basic_fixed_string<char, N>;

    template<std::size_t N>
    using fixed_wstring = basic_fixed_string<wchar_t, N>;

    template<std::size_t N>
    using fixed_u8string = basic_fixed_string<char8_t, N>;

    template<std::size_t N>
    using fixed_u16string = basic_fixed_string<char16_t, N>;

    template<std::size_t N>
    using fixed_u32string = basic_fixed_string<char32_t, N>;
}

#ifdef __cpp_lib_format
template<typename CharType, std::size_t N>
struct std::formatter<coio::basic_fixed_string<CharType, N>, CharType> : private std::formatter<std::basic_string_view<CharType>> {
private:
    using base = std::formatter<std::basic_string_view<CharType>>;

public:
    using base::parse;

    template<typename Out>
    auto format(const coio::basic_fixed_string<CharType, N>& str, std::basic_format_context<Out, CharType>& ctx) const {
        return base::format(str.view(), ctx);;
    }
};
#endif

template<typename CharType, std::size_t N>
struct std::hash<coio::basic_fixed_string<CharType, N>> : private std::hash<std::basic_string_view<CharType>> {
private:
    using base = std::hash<std::basic_string_view<CharType>>;
public:
    COIO_STATIC_CALL_OP auto operator() (const coio::basic_fixed_string<CharType, N>& str) COIO_STATIC_CALL_OP_CONST noexcept -> std::size_t {
        return base::operator()(str.view());
    }
};