#include <coio/core.h>
#include "common.h"

struct my_allocator_global_state {
    static constexpr std::size_t buffer_size = 1024;
    inline static std::byte buffer[buffer_size];
    inline static std::size_t offset = 0;
};

template<typename T = std::byte>
struct my_allocator : my_allocator_global_state {
    using value_type = T;

    using is_always_equal = std::true_type;

    template<typename U>
    struct rebind {
        using other = my_allocator<U>;
    };

    my_allocator() = default;

    my_allocator(const my_allocator&) = default;

    template<typename U>
    explicit my_allocator(const my_allocator<U>&) noexcept {}

    auto operator= (const my_allocator&) -> my_allocator& = default;

    static auto allocate(std::size_t n) -> T* {
        const std::size_t count = std::max(n, std::size_t{1}) * sizeof(T);
        void* current = buffer + offset;
        std::size_t remaining = buffer_size - offset;
        void* result = std::align(alignof(T), count, current, remaining);
        if (result == nullptr) throw std::bad_alloc{};
        offset = std::bit_cast<std::uintptr_t>(result) - std::bit_cast<std::uintptr_t>(+buffer) + count;
        return static_cast<T*>(result);
    }

    static auto deallocate(T*, std::size_t) noexcept {} // noop

    friend auto operator== (const my_allocator&, const my_allocator&) noexcept -> bool {
        return true;
    }
};

auto bar(std::allocator_arg_t, auto) -> coio::task<> {
    auto alloc = co_await coio::execution::read_env(coio::get_allocator);
    using string_allocator = typename std::allocator_traits<decltype(alloc)>::template rebind_alloc<char>;
    using string = std::basic_string<char, std::char_traits<char>, string_allocator>;
    // `str` will be allocated on `alloc`
    string str("bar\n*******************", std::move(alloc));
    ::println("{}", str);
}

auto foo(std::allocator_arg_t, auto) -> coio::task<> {
    ::println("foo");
    co_await bar(std::allocator_arg, co_await coio::execution::read_env(coio::get_allocator));
}

auto qux() -> coio::task<void, my_allocator<>> {
    ::println("qux");
    co_await bar(std::allocator_arg, co_await coio::execution::read_env(coio::get_allocator));
}

auto main() -> int {
    std::byte buffer[1024];
    std::pmr::monotonic_buffer_resource resource{buffer, std::ranges::size(buffer), std::pmr::null_memory_resource()};
    // the coroutine `foo`, and `bar` at line 58, will be allocated on `buffer`
    coio::this_thread::sync_wait(foo(std::allocator_arg, std::pmr::polymorphic_allocator<>{&resource}));

    // the coroutine `qux`, and `bar` at line 63, will be allocated on `my_allocator_global_state::buffer`
    coio::this_thread::sync_wait(qux());
}