#include <iomanip>
#include <iostream>
#include <sstream>
#include <coio/utils/json.h>

namespace {
    auto print_json_impl(std::ostream& out, const coio::json::value& v, std::size_t indent, std::size_t level) -> void;

    auto print_json_array(std::ostream& out, const coio::json::array& a, std::size_t indent, std::size_t level) -> void {
        out << '[';
        for (bool is_first = true; const auto& elem : a) {
            if (!is_first) out << ", ";
            is_first = false;
            print_json_impl(out, elem, indent, level + 1);
        }
        out << ']';
    }

    auto print_json_object(std::ostream& out, const coio::json::object& o, std::size_t indent, std::size_t level) -> void {
        out << "{\n";
        for (bool is_first = true; const auto& [k, v] : o) {
            if (!is_first) out << ",\n";
            is_first = false;
            out << std::string((level + 1) * indent, ' ') << std::quoted(k) << ": ";
            print_json_impl(out, v, indent, level + 1);
        }
        out << '\n' << std::string(level * indent, ' ') <<  "}";
    }

    auto print_json_impl(std::ostream& out, const coio::json::value& v, std::size_t indent, std::size_t level) -> void {
        v.visit([&out, indent, level]<typename T>(const T& d) {
            using U = std::remove_cvref_t<T>;
            if constexpr (std::same_as<U, coio::json::object>) {
                print_json_object(out, d, indent, level);
            }
            else if constexpr (std::same_as<U, coio::json::array>) {
                print_json_array(out, d, indent, level);
            }
            else if constexpr (std::same_as<U, coio::json::string>) {
                out << std::quoted(d);
            }
            else if constexpr (std::same_as<U, coio::json::number>) {
                if (std::holds_alternative<coio::json::integer>(d)) out << std::get<coio::json::integer>(d);
                else out << std::get<coio::json::floating>(d);
            }
            else if constexpr (std::same_as<U, coio::json::boolean>) {
                out << (d ? "true" : "false");
            }
            else if constexpr (std::same_as<U, coio::json::null>) {
                out << "null";
            }
        });
    }

    auto print_json(std::ostream& out, const coio::json::value& v, std::size_t indent = 2) -> void {
        print_json_impl(out, v, indent, 0);
    }
}

auto main() -> int {
    std::stringstream json{R"**(
        {
             "first-name": "John",
             "last-name": "Smith",
             "sex": "male",
             "age": 25,
             "address": {
                 "street-address": "21 2nd Street",
                 "city": "New York",
                 "state": "NY",
                 "postal-code": "10021"
             },
             "phone-number": [
                 {
                   "type": "home",
                   "number": "212 555-1234"
                 },
                 {
                   "type": "fax",
                   "number": "646 555-4567"
                 }
             ]
         }
    )**"};
    auto stream_view = std::views::istream<char>(json);
    auto json_value = coio::json::parse(stream_view);
    ::print_json(std::cout, json_value);
}