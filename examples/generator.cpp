#include <memory_resource>
#include "common.h"

auto fibonacci(std::size_t n) ->coio::generator<int> {
	int a = 0, b = 1;
	while (n--) {
		co_yield b;
		a = std::exchange(b, a + b);
	}
}

auto pmr_fibonacci(std::allocator_arg_t, std::pmr::polymorphic_allocator<>, std::size_t n) ->coio::generator<int, void, std::pmr::polymorphic_allocator<>> {
	co_yield coio::elements_of{fibonacci(n)};
}

auto main() ->int {
	std::pmr::monotonic_buffer_resource mem;
	for (auto i : pmr_fibonacci(std::allocator_arg, &mem, 10)) {
		::println("{}", i);
	}
}