# Coroutines Guide

coio provides two coroutine types: `coio::task<T>` for asynchronous computation, and `coio::generator<Ref, Val>` for lazy synchronous sequences.

## task — Async Computations

`coio::task<T>` is a lazily-started coroutine. It does not begin execution until awaited or connected to a receiver.

### Returning Values

```cpp
#include <coio/task.h>
#include <coio/core.h>

auto add(int a, int b) -> coio::task<int> {
    co_return a + b;
}

auto caller() -> coio::task<> {
    int result = co_await add(40, 2);
    std::println("result = {}", result); // 42
}

int main() {
    coio::this_thread::sync_wait(caller());
}
```

### Tasks are Senders

A `task` is both a coroutine and a sender. You can mix coroutine and sender syntax:

```cpp
// Coroutine style
auto style1() -> coio::task<int> {
    int x = co_await compute_x();
    int y = co_await compute_y();
    co_return x + y;
}

// Sender composition style
auto style2(auto sched) -> coio::task<int> {
    co_return co_await sched.schedule()
        | coio::then([]{ return 42; })
        | coio::then([](int x) { return x * 2; });
}
```

### Error Propagation

Exceptions thrown in tasks are captured as `std::exception_ptr` and re-thrown when awaited:

```cpp
auto might_fail() -> coio::task<int> {
    throw std::runtime_error("oops");
    co_return 42; // never reached
}

auto handle_error() -> coio::task<> {
    try {
        co_await might_fail();
    } catch (const std::runtime_error& e) {
        std::println("Caught: {}", e.what());
    }
}
```

### Awaiting Senders

Inside a `task`, you can `co_await` any sender. The library handles the sender-to-awaitable conversion:

```cpp
auto await_sender_example(auto sched) -> coio::task<> {
    // Await a schedule sender
    co_await sched.schedule();

    // Await a composed sender
    int val = co_await(
        sched.schedule()
        | coio::then([] { return 100; })
    );
    std::println("value = {}", val);
}
```

## generator — Lazy Sequences

`coio::generator<Ref, Val>` is a synchronous generator with `co_yield`.

### Basic Usage

```cpp
#include <coio/generator.h>

auto fibonacci(std::size_t n) -> coio::generator<int> {
    int a = 0, b = 1;
    while (n--) {
        co_yield b;
        a = std::exchange(b, a + b);
    }
}

for (int x : fibonacci(10)) {
    std::print("{} ", x);
}
// Output: 1 1 2 3 5 8 13 21 34 55
```

### Recursive Generators

Use `coio::elements_of` to yield from sub-generators:

```cpp
struct Node {
    int value;
    std::vector<Node> children;
};

auto traverse(const Node& node) -> coio::generator<int> {
    co_yield node.value;
    for (const auto& child : node.children) {
        co_yield coio::elements_of{traverse(child)};
    }
}
```

### Yielding Ranges

`elements_of` also works with any range:

```cpp
auto flatten_ranges() -> coio::generator<int> {
    std::vector v1{1, 2, 3};
    std::vector v2{4, 5, 6};
    co_yield coio::elements_of{v1};
    co_yield coio::elements_of{v2};
    // yields: 1, 2, 3, 4, 5, 6
}
```

### Generators are Ranges

Since `generator` inherits from `std::ranges::view_interface`, you can use range algorithms:

```cpp
auto evens = fibonacci(10)
    | std::views::filter([](int x) { return x % 2 == 0; });
```

## Custom Allocators

Both `task` and `generator` support custom allocators:

```cpp
template<typename T = std::byte>
struct arena_allocator {
    // ... standard allocator interface
};

auto my_coro() -> coio::task<void, arena_allocator<>> {
    // Coroutine frame allocated via arena_allocator
    co_return;
}
```

## Bridging to sync_wait

The main function (or any non-coroutine context) uses `sync_wait` to bridge into the async world:

```cpp
int main() {
    // From sync code into async task
    int result = coio::this_thread::sync_wait(
        coio::just() | coio::then([] { return 42; })
    );
    assert(result == 42);

    // Or with a task
    coio::this_thread::sync_wait(add(40, 2));
}
```
