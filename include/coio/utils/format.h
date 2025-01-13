#pragma once
#ifdef __cpp_lib_format
#include <format>

namespace coio {
    struct no_specification_formatter {
        constexpr auto parse(std::format_parse_context& ctx) {
            auto [first, last] = std::ranges::subrange{ctx};
            if (first == last or *first == '}') return first;
            throw std::format_error{"invalid format specification."};
        }
    };

    struct no_specification_wformatter {
        constexpr auto parse(std::wformat_parse_context& ctx) {
            auto [first, last] = std::ranges::subrange{ctx};
            if (first == last or *first == L'}') return first;
            throw std::format_error{"invalid format specification."};
        }
    };
}

#endif