#include <chrono>
#include <memory_resource>
#include "common.h"

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

int a = 114;
int b = 514;

auto return_a() ->coio::task<int&> {
	using namespace std::chrono_literals;
	co_await 1ms;
	co_return a;
}

auto return_b() ->coio::task<int&> {
	using namespace std::chrono_literals;
	co_await 500ms;
	co_return b;
}

auto return_hello(std::allocator_arg_t, std::pmr::polymorphic_allocator<> alloc) ->coio::task<std::pmr::string, std::pmr::polymorphic_allocator<>> {
	using namespace std::chrono_literals;
	co_await 500ms;
	co_return std::pmr::string("hello", alloc);
}

auto return_hello_world(std::allocator_arg_t, std::pmr::polymorphic_allocator<> alloc) ->coio::task<std::pmr::string, std::pmr::polymorphic_allocator<>> {
	co_return co_await return_hello(std::allocator_arg, alloc) + " world";
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
	{
		::println("a + b = {}", co_await return_a() + co_await return_b());
		auto [a, b] = co_await coio::when_all(return_a(), return_b());
		::println("local: a = {}, b = {}", a, b);
		std::ranges::swap(a, b);
		::println("global: a = {}, b = {}", ::a, ::b);
	}
	{
		std::pmr::monotonic_buffer_resource mem;
		::println("{}", co_await return_hello_world(std::allocator_arg, &mem));
	}
}

auto main() ->int {
	coio::run_loop& loop = coio::run_loop::instance();
	loop.run_until_complete(co_main());
}
