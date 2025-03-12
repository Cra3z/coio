#pragma once
#include <charconv>
#include <concepts>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include "../config.h"
#include "type_traits.h"

namespace coio::json {
    class value;
    using null = std::nullptr_t;
    using boolean = bool;
    using integer = std::ptrdiff_t;
    using floating = float;
    using number = std::variant<integer, floating>;
    using string = std::string;
    using array = std::vector<value>;
    using object = std::unordered_map<string, value>;

    struct parse_error : std::runtime_error {
        using runtime_error::runtime_error;
    };

    class value {
    private:
        using types_ = type_list<null, boolean, number, string, array, object>;
        using variant_t_ = types_::apply<std::variant>;

    public:
        template<typename T> requires (not std::same_as<std::remove_cvref_t<T>, value>) and std::constructible_from<variant_t_, T>
        explicit(not types_::contains<std::remove_cvref_t<T>>) value(T&& v) noexcept(std::is_nothrow_constructible_v<variant_t_, T>) : data_(std::forward<T>(v)) {}

        template<typename T> requires (types_::contains<std::remove_cvref_t<T>>) or std::same_as<T, integer> or std::same_as<T, floating>
        auto is() const noexcept -> bool {
            if constexpr (std::same_as<T, integer> or std::same_as<T, floating>) {
                return std::holds_alternative<number>(data_) and std::holds_alternative<T>(std::get<number>(data_));
            }
            else {
                return std::holds_alternative<T>(data_);
            }
        }

        template<typename T> requires (types_::contains<std::remove_cvref_t<T>>) or std::same_as<T, integer> or std::same_as<T, floating>
        decltype(auto) as() & {
            if constexpr (std::same_as<T, integer> or std::same_as<T, floating>) {
                return std::get<T>(std::get<number>(data_));
            }
            else {
                return std::get<T>(data_);
            }
        }

        template<typename T> requires (types_::contains<std::remove_cvref_t<T>>) or std::same_as<T, integer> or std::same_as<T, floating>
        decltype(auto) as() const& {
            if constexpr (std::same_as<T, integer> or std::same_as<T, floating>) {
                return std::get<T>(std::get<number>(data_));
            }
            else {
                return std::get<T>(data_);
            }
        }

        template<typename T> requires (types_::contains<std::remove_cvref_t<T>>) or std::same_as<T, integer> or std::same_as<T, floating>
        decltype(auto) as() && {
            if constexpr (std::same_as<T, integer> or std::same_as<T, floating>) {
                return std::move(std::get<T>(std::get<number>(data_)));
            }
            else {
                return std::move(std::get<T>(data_));
            }
        }

        template<typename T> requires (types_::contains<std::remove_cvref_t<T>>) or std::same_as<T, integer> or std::same_as<T, floating>
        decltype(auto) as() const&& {
            if constexpr (std::same_as<T, integer> or std::same_as<T, floating>) {
                return std::move(std::get<T>(std::get<number>(data_)));
            }
            else {
                return std::move(std::get<T>(data_));
            }
        }

        template<typename Fn> requires requires { std::visit(std::declval<Fn>(), std::declval<variant_t_&>()); }
        decltype(auto) visit(Fn&& fn) & {
            return std::visit(std::forward<Fn>(fn), data_);
        }

        template<typename Fn> requires requires { std::visit(std::declval<Fn>(), std::declval<const variant_t_&>()); }
        decltype(auto) visit(Fn&& fn) const& {
            return std::visit(std::forward<Fn>(fn), data_);
        }

        template<typename Fn> requires requires { std::visit(std::declval<Fn>(), std::declval<variant_t_&&>()); }
        decltype(auto) visit(Fn&& fn) && {
            return std::visit(std::forward<Fn>(fn), std::move(data_));
        }

        template<typename Fn> requires requires { std::visit(std::declval<Fn>(), std::declval<const variant_t_&&>()); }
        decltype(auto) visit(Fn&& fn) const&& {
            return std::visit(std::forward<Fn>(fn), std::move(data_));
        }

    private:
        variant_t_ data_;
    };

    template<std::input_iterator It, std::sentinel_for<It> St> requires std::convertible_to<std::iter_value_t<It>, char>
    class parser {
    public:
        parser(It it, St st) noexcept(std::is_nothrow_constructible_v<std::ranges::subrange<It, St>, It, St>) : source_(std::move(it), std::move(st)) {}

        template<typename Range> requires std::ranges::borrowed_range<Range> and std::ranges::input_range<Range> and std::same_as<std::ranges::iterator_t<Range>, It> and std::same_as<std::ranges::sentinel_t<Range>, St>
        explicit parser(Range&& range) noexcept(std::is_nothrow_constructible_v<std::ranges::subrange<It, St>, It, St>) : parser(std::ranges::begin(range), std::ranges::end(range)) {}

        [[nodiscard]]
        auto parse() -> value {
            auto value_ = parse_value_();
            if (not done_()) throw parse_error{"can't parse a complete value"};
            return value_;
        }

    private:
        auto advance_(std::ptrdiff_t n = 1) noexcept -> void {
            source_.advance(n);
        }

        auto peek_() noexcept -> char {
            return *source_.begin();
        }

        auto get_() noexcept -> char {
            auto result = peek_();
            advance_();
            return result;
        }

        auto done_() -> bool {
            return source_.empty();
        }

        auto skip_white_spaces_() noexcept -> void {
            while (not done_() and std::isspace(peek_())) advance_();
        }

        auto next_n_characters_are_(std::string_view str) noexcept -> bool {
            for (auto chr : str) {
                if (done_() or get_() != chr) return false;
            }
            return true;
        }

        auto parse_value_() -> value {
            skip_white_spaces_();
            auto peek = peek_();
            if (peek == 'n') return parse_null_();
            if (peek == 't') return parse_true_();
            if (peek == 'f') return parse_false_();
            if (peek == '"') return parse_string_();
            if (peek == '[') return parse_array_();
            if (peek == '{') return parse_object_();
            if (('0' < peek and peek < '9') or peek == '-') return parse_number_();
            throw parse_error{"invalid json"};
        }

        auto parse_null_() -> value {
            if (next_n_characters_are_("null")) return nullptr;
            throw parse_error{"expected `null` isn't complete"};
        }

        auto parse_true_() -> value {
            if (next_n_characters_are_("true")) return true;
            throw parse_error{"expected `true` isn't complete"};
        }

        auto parse_false_() -> value {
            if (next_n_characters_are_("false")) return false;
            throw parse_error{"expected `false` isn't complete"};
        }

        auto parse_number_() -> value {
            string number_string;
            bool is_floating = false;
            while (not done_()) {
                auto peek = peek_();
                if (peek == '.') is_floating = true;
                if (std::isdigit(peek) or peek == '.' or peek == '-') {
                    number_string.push_back(peek);
                    advance_();
                }
                else break;
            }
            auto first = number_string.data();
            auto last = number_string.data() + number_string.size();
            number result{std::in_place_type<integer>};
            if (is_floating) {
                result.emplace<floating>();
                auto [_, ec] = std::from_chars(first, last, std::get<floating>(result));
                if (ec != std::errc{}) throw parse_error{"invalid floating pointer"};
            }
            else {
                auto [_, ec] = std::from_chars(first, last, std::get<integer>(result));
                if (ec != std::errc{}) throw parse_error{"invalid integer"};
            }
            skip_white_spaces_();
            return result;
        }

        auto parse_string_() -> value {
            advance_();
            string result;

            auto handle_escape_sequence = [this, &result] {
                if (done_()) throw parse_error{"unexpected end of string"};
                switch (char escaped_char = get_(); escaped_char) {
                case '\"': result += '\"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': throw parse_error{"unsupported unicode escape sequence"}; // TODO: support unicode escape sequence
                default: throw parse_error{"invalid escape sequence"};
                }
            };

            while (not done_()) {
                char ch = get_();
                if (ch == '\"') break;
                if (ch == '\\') handle_escape_sequence();
                else result += ch;
            }
            skip_white_spaces_();
            return result;
        }

        auto parse_array_() -> value {
            advance_(); // skip '['
            array result;
            skip_white_spaces_();
            while (peek_() != ']') {
                result.push_back(parse_value_());
                skip_white_spaces_();
                if (peek_() == ']') break;
                if (get_() != ',') throw parse_error{"expected comma in array"};
            }
            advance_(); // skip ']'
            skip_white_spaces_();
            return result;
        }

        auto parse_object_() -> value {
            advance_(); // skip '{'
            object result;
            skip_white_spaces_();
            while (peek_() != '}') {
                skip_white_spaces_();
                std::string key = parse_string_().template as<string>();
                skip_white_spaces_();
                if (get_() != ':') throw parse_error{"expected colon in object"};
                skip_white_spaces_();
                result.emplace(key, parse_value_());
                skip_white_spaces_();
                if (peek_() == '}') break;
                if (get_() != ',') throw parse_error{"expected comma in object"};
            }
            advance_(); // skip '}'
            skip_white_spaces_();
            return result;
        }

    private:
        std::ranges::subrange<It, St> source_;
    };

    template<typename Range> requires std::ranges::borrowed_range<Range> and std::ranges::input_range<Range> and std::convertible_to<std::ranges::range_value_t<Range>, char>
    explicit parser(Range&&) -> parser<std::ranges::iterator_t<Range>, std::ranges::sentinel_t<Range>>;

    namespace detail {
        struct parse_fn {
            template<typename Range> requires std::ranges::borrowed_range<Range> and std::ranges::input_range<Range> and std::convertible_to<std::ranges::range_value_t<Range>, char>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Range&& source) COIO_STATIC_CALL_OP_CONST -> value {
                return parser{std::forward<Range>(source)}.parse();
            }

            template<std::input_iterator It, std::sentinel_for<It> St> requires std::convertible_to<std::iter_value_t<It>, char>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (It it, St st) COIO_STATIC_CALL_OP_CONST -> value {
                return parser{std::move(it), std::move(st)}.parse();
            }
        };
    }

    inline constexpr detail::parse_fn parse;

}