#include "common.h"

auto fibonacci(std::size_t n) ->coio::generator<int> {
	int a = 0, b = 1;
	while (n--) {
		co_yield b;
		a = std::exchange(b, a + b);
	}
}

auto main() ->int {
	for (auto i : fibonacci(10)) {
		::println("{}", i);
	}
}