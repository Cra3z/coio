#include <chrono>
#include <memory_resource>
#include <coio/core.h>
#include <coio/utils/scope_exit.h>
#include "common.h"

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

auto main() ->int {
    using namespace std::chrono_literals;
    std::pmr::monotonic_buffer_resource mem;
    auto hello = [](std::allocator_arg_t, std::pmr::polymorphic_allocator<> alloc) ->coio::shared_task<std::pmr::string, std::pmr::polymorphic_allocator<>> {
        co_await 2s;
        co_return std::pmr::string{"hello", alloc};
    }(std::allocator_arg, &mem);
    [[maybe_unused]] timekeeper guard;
    auto [res1, res2, _] = coio::sync_wait(coio::when_all(
        [&hello](std::allocator_arg_t, std::pmr::polymorphic_allocator<> alloc) ->coio::task<std::pmr::string, std::pmr::polymorphic_allocator<>> {
            co_await 1s;
            co_return std::pmr::string{co_await hello + " coroutine", alloc};
        }(std::allocator_arg, &mem),
        [&hello](std::allocator_arg_t, std::pmr::polymorphic_allocator<> alloc) ->coio::task<std::pmr::string, std::pmr::polymorphic_allocator<>> {
            co_await 3s;
            co_return std::pmr::string{co_await hello + " coio", alloc};
        }(std::allocator_arg, &mem),
        [&io_context = coio::io_context::instance()](std::allocator_arg_t, std::pmr::polymorphic_allocator<>) ->coio::task<void, std::pmr::polymorphic_allocator<>> {
            co_return io_context.run();
        }(std::allocator_arg, &mem)
    ));
    ::println("{}, {}", res1, res2);
}
