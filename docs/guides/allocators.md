# Allocators Guide

coio supports custom allocators for coroutine frames, tasks, and async operations.

## Coroutine Frame Allocation

Pass an allocator to `task` to control where the coroutine frame is allocated:

```cpp
#include <coio/task.h>
#include <coio/core.h>

template<typename T = std::byte>
struct arena_allocator {
    using value_type = T;
    using is_always_equal = std::true_type;

    template<typename U>
    struct rebind { using other = arena_allocator<U>; };

    arena_allocator() = default;
    template<typename U>
    explicit arena_allocator(const arena_allocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        // Allocate from your arena/pool
        return arena_alloc(n * sizeof(T));
    }

    void deallocate(T*, std::size_t) noexcept {
        // No-op or return to pool
    }

    friend bool operator==(const arena_allocator&,
                           const arena_allocator&) noexcept {
        return true;
    }
};

// A task using the custom allocator
auto my_task() -> coio::task<void, arena_allocator<>> {
    // Coroutine frame allocated by arena_allocator
    co_return;
}
```

## Propagating Allocators with the Environment

Use `std::allocator_arg` to propagate allocators through the environment:

```cpp
auto child_task(std::allocator_arg_t, auto alloc) -> coio::task<> {
    // Read the allocator from the environment
    auto env_alloc = co_await coio::execution::read_env(
        coio::get_allocator);

    // Use it for allocations within this task
    using string_alloc = typename std::allocator_traits<
        decltype(env_alloc)>::template rebind_alloc<char>;
    using string = std::basic_string<char, std::char_traits<char>,
                                      string_alloc>;

    string msg{"Allocated with custom allocator", std::move(env_alloc)};
    std::println("{}", msg);
}

int main() {
    // Create a monotonic buffer
    std::byte buffer[4096];
    std::pmr::monotonic_buffer_resource resource{
        buffer, sizeof(buffer), std::pmr::null_memory_resource()};

    auto alloc = std::pmr::polymorphic_allocator<>{&resource};

    // Pass allocator via std::allocator_arg
    coio::this_thread::sync_wait(
        child_task(std::allocator_arg, alloc)
    );
}
```

## Chaining with Allocators

Allocators are automatically propagated through `co_await` chains:

```cpp
auto foo(std::allocator_arg_t, auto alloc) -> coio::task<> {
    // bar will inherit the same allocator
    co_await bar(std::allocator_arg,
        co_await coio::execution::read_env(coio::get_allocator));
}
```

## Polymorphic Allocators

coio contexts use `std::pmr::polymorphic_allocator` internally. You can configure a context with a custom memory resource:

```cpp
#include <coio/execution_context.h>

// Simple pooled memory resource (example)
class pool_resource : public std::pmr::memory_resource {
    void* do_allocate(size_t bytes, size_t align) override { ... }
    void do_deallocate(void* p, size_t bytes, size_t align) override { ... }
    bool do_is_equal(const memory_resource& other) const noexcept override { ... }
};

pool_resource pool;
coio::time_loop ctx{pool}; // uses pool_resource for internal allocations
```

## Using allocator_resource

Adapt any `simple_allocator` into a `memory_resource`:

```cpp
#include <coio/utils/allocator_resource.h>

arena_allocator<> arena;
coio::allocator_resource resource{arena};

// Use with polymorphic_allocator
std::pmr::polymorphic_allocator<> alloc{&resource};
```

## Allocator Requirements

A `simple_allocator` must provide:

```cpp
template<typename T>
struct simple_allocator {
    using value_type = T;
    using is_always_equal = /* std::true_type or std::false_type */;

    template<typename U>
    struct rebind { using other = simple_allocator<U>; };

    T* allocate(std::size_t n);
    void deallocate(T* p, std::size_t n);

    bool operator==(const simple_allocator& other) const noexcept;
};
```

## Best Practices

1. **Use monotonic/arena allocators** for tasks with known lifetimes to avoid per-allocation overhead
2. **Propagate allocators explicitly** via `std::allocator_arg` and `get_allocator`
3. **Use `std::pmr::monotonic_buffer_resource`** for quick scratch-space allocation
4. **Pool allocators** for repeated allocations of the same size (timer callbacks, I/O operation states)
