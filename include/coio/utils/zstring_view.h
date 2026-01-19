#pragma once
#include <string_view>

namespace coio {
    template<typename CharType, typename CharTraits = std::char_traits<CharType>>
    class basic_zstring_view : private std::basic_string_view<CharType, CharTraits> {
    private:
        using base = std::basic_string_view<CharType, CharTraits>;
    public:
        using traits_type            = typename base::traits_type;
        using value_type             = typename base::value_type;
        using pointer                = typename base::pointer;
        using const_pointer          = typename base::const_pointer;
        using reference              = typename base::reference;
        using const_reference        = typename base::const_reference;
        using const_iterator         = typename base::const_iterator;
        using iterator               = typename base::iterator;
        using const_reverse_iterator = typename base::const_reverse_iterator;
        using reverse_iterator       = typename base::reverse_iterator;
        using size_type              = typename base::size_type;
        using difference_type        = typename base::difference_type;

    public:
        constexpr basic_zstring_view(const CharType* c_str) noexcept : base(c_str) {}

        constexpr basic_zstring_view(const std::basic_string<CharType, CharTraits>& str) noexcept : base(str) {}

        using base::at;
        using base::back;
        using base::compare;
        using base::copy;
        using base::front;
        using base::operator[];
        using base::begin;
        using base::rbegin;
        using base::cbegin;
        using base::crbegin;
        using base::end;
        using base::rend;
        using base::cend;
        using base::crend;
        using base::size;
        using base::max_size;
        using base::length;
        using base::empty;
        using base::find;
        using base::rfind;
        using base::find_first_not_of;
        using base::find_last_not_of;
        using base::find_first_of;
        using base::find_last_of;
        using base::ends_with;
        using base::starts_with;

        friend constexpr auto operator== (const basic_zstring_view& lhs, const basic_zstring_view& rhs) noexcept -> bool = default;

        friend constexpr auto operator<=> (const basic_zstring_view& lhs, const basic_zstring_view& rhs) noexcept = default;

        [[nodiscard]]
        constexpr auto c_str() const noexcept -> const_pointer {
            return base::data();
        }

        [[nodiscard]]
        constexpr auto view() const noexcept -> base {
            return static_cast<const base&>(*this);
        }
    };

    using zstring_view = basic_zstring_view<char>;
}

template<typename CharType, typename CharTraits>
struct std::hash<coio::basic_zstring_view<CharType, CharTraits>> {
    auto operator()(coio::basic_zstring_view<CharType, CharTraits> zstring_view) const noexcept -> std::size_t {
        return std::hash<std::basic_string_view<CharType, CharTraits>>{}(zstring_view.view());
    }
};

#ifdef __cpp_lib_format
template<typename CharType, typename CharTraits>
struct std::formatter<coio::basic_zstring_view<CharType, CharTraits>, CharType> : std::formatter<std::basic_string_view<CharType, CharTraits>, CharType> {};
#endif