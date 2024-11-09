#include <iostream>
#include <sstream>
#include <format>
#include <array>
#include <algorithm>
#include <numeric>
#include <complex>
#include <ranges>
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
auto println(std::format_string<Args...> fmt, Args&&... args) ->void {
	std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}

auto after(int x) ->coio::task<int> {
	using namespace std::chrono_literals;
	co_await std::chrono::seconds{x};
	co_return x;
}

auto insert_after(std::vector<int>& out, int x) ->coio::task<> {
	out.push_back(co_await after(x));
}

auto sleep_sort(std::span<int> nums) ->coio::task<std::vector<int>> {
	std::vector<int> result;
	auto r = nums | std::views::transform([&](int x) {
		return insert_after(result, x);
	});
	co_await coio::when_all(r);
	co_return result;
}

class timekeeper {
public:
	COIO_ALWAYS_INLINE timekeeper() noexcept : begin(std::chrono::steady_clock::now()) {}

	COIO_ALWAYS_INLINE ~timekeeper() {
		auto end = std::chrono::steady_clock::now();
		::println("take {}s", float(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count()) / 1000.f);
	}
private:
	std::chrono::steady_clock::time_point begin;
};

auto co_main() ->coio::task<> {
	using namespace std::chrono_literals;
	{
		timekeeper _{};
		auto [i, j] = co_await coio::when_all(after(1), after(2)); // take 2s instead of 3s
		::println("{} + {} = {}", i, j, i + j);
	}
	{
		timekeeper _{};
		std::vector nums{3, 1, 4, 2, 2, 1, 0, 3, 2, 1};
		::println("{}", co_await sleep_sort(nums));
	}
}

auto main() ->int {
	coio::run_loop& loop = coio::run_loop::instance();
	loop.run_until_complete(co_main());
}
