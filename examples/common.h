#pragma once
#include <cassert>
#include <format>
#include <ranges>
#include <iostream>
#include <sstream>
#include <span>
#include <coio/coio.h>


#ifndef __cpp_lib_format_ranges
	template<typename T, typename Alloc>
	struct std::formatter<std::vector<T, Alloc>> {
		constexpr auto parse(std::format_parse_context& ctx) {
			auto [first, last] = std::tuple{ctx.begin(), ctx.end()};
			if (first == last or *first == '}') return first;
			throw std::format_error{"invalid format"};
		}

		auto format(const std::vector<T, Alloc>& vec, std::format_context& ctx) const {
			std::stringstream ss;
			ss << '[';
			if (not vec.empty()) {
				for (const auto& elem : vec | std::views::take(vec.size() - 1)) {
					ss << elem << ", ";
				}
				ss << vec.back();
			}
			ss << ']';
			return std::format_to(ctx.out(), "{}", ss.str());
		}
	};
#endif

template<typename... Args>
auto log(std::format_string<Args...> fmt, Args&&... args) ->void {
	std::flush(std::cout << std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
auto println(std::format_string<Args...> fmt, Args&&... args) ->void {
	std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}
