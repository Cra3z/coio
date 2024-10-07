#include <iostream>
#include <format>
#include <coio/core.h>

template<typename... Args>
auto println(std::format_string<Args...> fmt, Args&&... args) ->void {
	std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}

auto after(int x) ->coio::task<int> {
	using namespace std::chrono_literals;
	co_await std::chrono::seconds{x};
	co_return x;
}

auto co_main() ->coio::task<> {
	using namespace std::chrono_literals;
	auto [i, j] = co_await coio::when_all(after(1), after(2)); // take 2s instead of 3s
	::println("{} + {} = {}", i, j, i + j);
}

auto main() ->int {
	coio::run_loop& loop = coio::run_loop::instance();
	loop.run_until_complete(co_main());
}
