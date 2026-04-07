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
#include <coio/core.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include "../common.h"

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
namespace json_rpc {
    using io_context = coio::epoll_context;
}
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
namespace json_rpc {
    using io_context = coio::iocp_context;
}
#endif

namespace json_rpc {
    using tcp_socket = coio::tcp::socket<io_context::scheduler>;
    using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

    enum errc : int {
        parse_error      = -32700,
        invalid_request  = -32600,
        method_not_found = -32601,
        invalid_params   = -32602,
        internal_error   = -32603
    };

    class value;
    using null = std::nullptr_t;
    using boolean = bool;
    using integer = std::ptrdiff_t;
    using floating = double;
    using number = std::variant<integer, floating>;
    using string = std::string;
    using array = std::vector<value>;
    using object = std::unordered_map<string, value>;

    struct bad_json : std::runtime_error {
        using runtime_error::runtime_error;
    };

    class value {
    private:
        using types_ = coio::type_list<null, boolean, number, string, array, object>;
        using variant_t_ = types_::apply<std::variant>;

    public:
        template<typename T> requires (not std::same_as<std::remove_cvref_t<T>, value>) and std::constructible_from<variant_t_, T>
        value(T&& v) noexcept(std::is_nothrow_constructible_v<variant_t_, T>) : data_(std::forward<T>(v)) {}

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

        friend auto operator== (const value& lhs, const value& rhs) noexcept -> bool = default;

    private:
        variant_t_ data_;
    };

    inline auto dump(const value& value) -> std::string;

    namespace detail {
        inline auto escape_string(std::string_view str) -> std::string {
            std::string out;
            out.reserve(str.size() + 2);
            out += '"';
            for (char ch : str) {
                switch (ch) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<int>(ch) < 0x20) {
                        out += std::format("\\u{:04x}", static_cast<int>(ch));
                    }
                    else {
                        out += ch;
                    }
                }
            }
            out += '"';
            return out;
        }

        inline auto is_space(char ch) noexcept -> bool {
            return std::isspace(static_cast<unsigned char>(ch)) != 0;
        }

        inline auto is_digit(char ch) noexcept -> bool {
            return std::isdigit(static_cast<unsigned char>(ch)) != 0;
        }

        inline auto dump_array(const array& arr) -> std::string {
            std::string out = "[";
            for (bool first = true; const auto& elem : arr) {
                if (!first) out += ',';
                first = false;
                out += dump(elem);
            }
            return out += ']';
        }

        inline auto dump_object(const object& obj) -> std::string {
            std::string out = "{";
            for (bool first = true; const auto& [k, val] : obj) {
                if (!first) out += ',';
                first = false;
                out += escape_string(k);
                out += ':';
                out += dump(val);
            }
            return out += '}';
        }

        template<std::input_iterator It, std::sentinel_for<It> St> requires std::convertible_to<std::iter_value_t<It>, char>
        class parser {
        public:
            parser(It it, St st) noexcept(std::is_nothrow_constructible_v<std::ranges::subrange<It, St>, It, St>) : source_(std::move(it), std::move(st)) {}

            template<typename Range> requires std::ranges::borrowed_range<Range> and std::ranges::input_range<Range> and std::same_as<std::ranges::iterator_t<Range>, It> and std::same_as<std::ranges::sentinel_t<Range>, St>
            explicit parser(Range&& range) noexcept(std::is_nothrow_constructible_v<std::ranges::subrange<It, St>, It, St>) : parser(std::ranges::begin(range), std::ranges::end(range)) {}

            [[nodiscard]]
            auto parse() -> value {
                auto value_ = parse_value_();
                skip_white_spaces_();
                if (not done_()) throw bad_json{"can't parse a complete value"};
                return value_;
            }

        private:
            auto advance_(std::ptrdiff_t n = 1) noexcept -> void {
                source_.advance(n);
            }

            auto peek_() -> char {
                if (done_()) throw bad_json{"unexpected end of input"};
                return *source_.begin();
            }

            auto get_() -> char {
                auto result = peek_();
                advance_();
                return result;
            }

            auto done_() -> bool {
                return source_.empty();
            }

            auto skip_white_spaces_() noexcept -> void {
                while (not done_() and is_space(peek_())) advance_();
            }

            auto next_n_characters_are_(std::string_view str) noexcept -> bool {
                for (auto chr : str) {
                    if (done_() or get_() != chr) return false;
                }
                return true;
            }

            auto parse_value_() -> value {
                skip_white_spaces_();
                if (done_()) throw bad_json{"unexpected end of input"};
                auto peek = peek_();
                if (peek == 'n') return parse_null_();
                if (peek == 't') return parse_true_();
                if (peek == 'f') return parse_false_();
                if (peek == '"') return parse_string_();
                if (peek == '[') return parse_array_();
                if (peek == '{') return parse_object_();
                if (('0' <= peek and peek <= '9') or peek == '-') return parse_number_();
                throw bad_json{"invalid json"};
            }

            auto parse_null_() -> value {
                if (next_n_characters_are_("null")) return nullptr;
                throw bad_json{"expected `null` isn't complete"};
            }

            auto parse_true_() -> value {
                if (next_n_characters_are_("true")) return true;
                throw bad_json{"expected `true` isn't complete"};
            }

            auto parse_false_() -> value {
                if (next_n_characters_are_("false")) return false;
                throw bad_json{"expected `false` isn't complete"};
            }

            auto parse_number_() -> value {
                string number_string;
                bool is_floating = false;

                if (peek_() == '-') number_string.push_back(get_());

                if (done_()) throw bad_json{"invalid number"};
                if (peek_() == '0') {
                    number_string.push_back(get_());
                    if (not done_() and is_digit(peek_())) {
                        throw bad_json{"leading zeros are not allowed"};
                    }
                }
                else if ('1' <= peek_() and peek_() <= '9') {
                    do {
                        number_string.push_back(get_());
                    } while (not done_() and is_digit(peek_()));
                }
                else {
                    throw bad_json{"invalid number"};
                }

                if (not done_() and peek_() == '.') {
                    is_floating = true;
                    number_string.push_back(get_());
                    if (done_() or not is_digit(peek_())) {
                        throw bad_json{"fractional part requires at least one digit"};
                    }
                    do {
                        number_string.push_back(get_());
                    } while (not done_() and is_digit(peek_()));
                }

                if (not done_() and (peek_() == 'e' or peek_() == 'E')) {
                    is_floating = true;
                    number_string.push_back(get_());
                    if (not done_() and (peek_() == '+' or peek_() == '-')) {
                        number_string.push_back(get_());
                    }
                    if (done_() or not is_digit(peek_())) {
                        throw bad_json{"exponent requires at least one digit"};
                    }
                    do {
                        number_string.push_back(get_());
                    } while (not done_() and is_digit(peek_()));
                }

                auto first = number_string.data();
                auto last = number_string.data() + number_string.size();
                number result{std::in_place_type<integer>};
                if (is_floating) {
                    result.emplace<floating>();
                    auto [ptr, ec] = std::from_chars(first, last, std::get<floating>(result));
                    if (ec != std::errc{} or ptr != last) throw bad_json{"invalid floating point"};
                }
                else {
                    auto [ptr, ec] = std::from_chars(first, last, std::get<integer>(result));
                    if (ec != std::errc{} or ptr != last) throw bad_json{"invalid integer"};
                }
                return result;
            }

            auto parse_string_() -> value {
                if (get_() != '"') throw bad_json{"expected string"};
                string result;

                auto handle_escape_sequence = [this, &result] {
                    if (done_()) throw bad_json{"unexpected end of string"};
                    switch (char escaped_char = get_(); escaped_char) {
                    case '\"': result += '\"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': throw bad_json{"unsupported unicode escape sequence"}; // TODO: support unicode escape sequence
                    default: throw bad_json{"invalid escape sequence"};
                    }
                };

                while (true) {
                    if (done_()) throw bad_json{"unexpected end of string"};
                    char ch = get_();
                    if (ch == '\"') break;
                    if (static_cast<unsigned char>(ch) < 0x20) {
                        throw bad_json{"control characters must be escaped"};
                    }
                    if (ch == '\\') handle_escape_sequence();
                    else result += ch;
                }
                return result;
            }

            auto parse_array_() -> value {
                advance_(); // skip '['
                array result;
                skip_white_spaces_();
                if (done_()) throw bad_json{"unexpected end of array"};
                if (peek_() == ']') {
                    advance_();
                    skip_white_spaces_();
                    return result;
                }

                while (true) {
                    result.push_back(parse_value_());
                    skip_white_spaces_();
                    if (done_()) throw bad_json{"unexpected end of array"};
                    auto delimiter = get_();
                    if (delimiter == ']') break;
                    if (delimiter != ',') throw bad_json{"expected comma in array"};
                    skip_white_spaces_();
                    if (done_()) throw bad_json{"unexpected end of array"};
                    if (peek_() == ']') throw bad_json{"trailing comma in array"};
                }
                skip_white_spaces_();
                return result;
            }

            auto parse_object_() -> value {
                advance_(); // skip '{'
                object result;
                skip_white_spaces_();
                if (done_()) throw bad_json{"unexpected end of object"};
                if (peek_() == '}') {
                    advance_();
                    skip_white_spaces_();
                    return result;
                }

                while (true) {
                    if (peek_() != '"') throw bad_json{"expected string key"};
                    std::string key = parse_string_().template as<string>();
                    skip_white_spaces_();
                    if (get_() != ':') throw bad_json{"expected colon in object"};
                    skip_white_spaces_();
                    result.insert_or_assign(std::move(key), parse_value_());
                    skip_white_spaces_();
                    if (done_()) throw bad_json{"unexpected end of object"};
                    auto delimiter = get_();
                    if (delimiter == '}') break;
                    if (delimiter != ',') throw bad_json{"expected comma in object"};
                    skip_white_spaces_();
                    if (done_()) throw bad_json{"unexpected end of object"};
                    if (peek_() == '}') throw bad_json{"trailing comma in object"};
                }
                skip_white_spaces_();
                return result;
            }

        private:
            std::ranges::subrange<It, St> source_;
        };

        template<typename Range> requires std::ranges::borrowed_range<Range> and std::ranges::input_range<Range> and std::convertible_to<std::ranges::range_value_t<Range>, char>
        explicit parser(Range&&) -> parser<std::ranges::iterator_t<Range>, std::ranges::sentinel_t<Range>>;

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

    inline auto dump(const value& value) -> std::string {
        return value.visit([]<typename T>(const T& data) -> std::string {
            if constexpr (std::same_as<T, null>) {
                return "null";
            }
            else if constexpr (std::same_as<T, boolean>) {
                return data ? "true" : "false";
            }
            else if constexpr (std::same_as<T, number>) {
                if (std::holds_alternative<integer>(data)) {
                    return std::to_string(std::get<integer>(data));
                }
                else {
                    return std::to_string(std::get<floating>(data));
                }
            }
            else if constexpr (std::same_as<T, string>) {
                return detail::escape_string(data);
            }
            else if constexpr (std::same_as<T, array>) {
                return detail::dump_array(data);
            }
            else if constexpr (std::same_as<T, object>) {
                return detail::dump_object(data);
            }
            else {
                coio::unreachable();
            }
        });
    }
}