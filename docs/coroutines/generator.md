# generator

`coio::generator<Ref, Val, Alloc>` is a synchronous coroutine generator: a lazily evaluated, single-pass range whose elements are produced by `co_yield`. It matches the design of C++23 `std::generator` ([P2502](https://wg21.link/p2502)) but works in C++20.

Header: `#include <coio/generator.h>`

## Overview

Use `generator` to write lazy sequences as ordinary loops instead of hand-rolled iterator state machines. A `generator`:

- models an **input range** (and derives from `std::ranges::view_interface`, so it is a view and composes with `std::views` adaptors);
- is **move-only** and **single-pass** — one traversal, `begin()` called at most once;
- supports **recursive generation**: `co_yield coio::elements_of{inner}` yields every element of a nested generator (with O(1) suspension depth via symmetric transfer) or of any input range;
- supports **allocator customization** of the coroutine frame via the leading-allocator-argument convention.

`generator` is synchronous: `co_await` is not permitted inside a generator body (its promise deletes `await_transform`). For asynchronous work, use [task](task.md).

## Synopsis

```cpp
namespace coio {
    template<std::ranges::range Range, typename Alloc = std::allocator<std::byte>>
    struct elements_of {
        Range range;
        Alloc allocator;
    };

    template<typename Range, typename Alloc = std::allocator<std::byte>>
    elements_of(Range&&, Alloc = Alloc()) -> elements_of<Range&&, Alloc>;

    template<typename Ref, typename Val = void, typename Alloc = void>
    class generator : public std::ranges::view_interface<generator<Ref, Val, Alloc>> {
        // exposition only:
        // value_type = Val, or std::remove_cvref_t<Ref> if Val is void
        // reference  = Ref, or Ref&& if Val is void
    public:
        using yielded = /*reference if it is a reference type, otherwise const reference&*/;
        using promise_type = /*unspecified*/;

        class iterator {
        public:
            using value_type = /*see above*/;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::input_iterator_tag;

            iterator(iterator&& other) noexcept;
            auto operator= (iterator&& other) noexcept -> iterator&;

            auto operator* () const -> /*reference*/;
            auto operator++ () -> iterator&;
            auto operator++ (int) -> void;

            friend auto operator== (const iterator&, std::default_sentinel_t) noexcept -> bool;
        };

        generator(const generator&) = delete;
        generator(generator&& other) noexcept;
        ~generator();
        auto operator= (generator other) noexcept -> generator&;

        auto begin() -> iterator;
        auto end() const noexcept -> std::default_sentinel_t;
    };
}
```

## API Reference

### Template parameters

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `Ref` | — | determines the reference/value produced per element |
| `Val` | `void` | explicit value type; when `void`, derived from `Ref` |
| `Alloc` | `void` | coroutine-frame allocation policy |

As in P2502, the common cases are:

- `generator<T>` — yields `T&&`, value type `T`. Cheapest general-purpose form.
- `generator<T&>` — yields mutable references to the caller.
- `generator<const T&>` — yields const references.
- `generator<T, U>` — reference type `T`, value type `U` (for proxy references, e.g. `generator<std::string_view, std::string>`).

`Alloc` must be `void` (accept any allocator per call site, type-erased) or a concrete allocator type whose `pointer` is a raw pointer. Constraints on the resulting value/reference types are enforced with `static_assert`s mirroring P2502.

### Yielding values

Within the body, `co_yield` accepts:

```cpp
co_yield expr;                       // expr convertible to `yielded`
co_yield elements_of{gen};           // every element of a nested generator (rvalue)
co_yield elements_of{range};         // every element of an input range
co_yield elements_of{range, alloc};  // ditto, nested frame allocated with `alloc`
```

- The plain form suspends after storing a handle to the yielded value; the value must stay alive until the generator is resumed (which the caller's iteration guarantees).
- When `yielded` is an rvalue-reference type, a `const` lvalue may also be yielded; the generator materializes a copy for the duration of the suspension.
- `elements_of{gen}` requires the inner generator to have the same `yielded` type. Resumption of deeply nested generators is O(1): the caller resumes the innermost active generator directly.
- `elements_of{range}` adapts any input range whose reference type is convertible to `yielded`, internally wrapping it into a nested generator (allocated with the given allocator).

An exception escaping the body is rethrown to the consumer from `begin()` or `iterator::operator++`. An exception thrown inside a nested generator surfaces first at the enclosing `co_yield elements_of{...}` expression — the enclosing generator may catch it there; if it does not, the exception continues outward to the consumer.

### Iteration model

```cpp
auto begin() -> iterator;
auto end() const noexcept -> std::default_sentinel_t;
```

`begin()` resumes the coroutine to its first `co_yield` and returns an iterator positioned there.

**Preconditions / contracts:**

- `begin()` may be called **at most once**, on a non-moved-from generator. A range-based `for` loop does the right thing.
- The range is **single-pass**: incrementing an iterator invalidates any previous element reference; iterators are move-only.
- `operator*` returns the current element; calling it on an iterator that compares equal to `std::default_sentinel` is undefined behavior.
- All iterators are invalidated when the generator is destroyed. The generator must outlive its iterators and any references obtained from `operator*`.

**Thread safety:** a generator and its iterators are not thread-safe; drive a given generator from one thread at a time.

### Allocator support

Like `task`, the frame follows the leading-allocator-argument convention:

```cpp
auto gen(std::allocator_arg_t, MyAlloc alloc, int n) -> coio::generator<int>;      // Alloc = void: any allocator
auto gen2(std::allocator_arg_t, MyAlloc alloc, int n) -> coio::generator<int, void, MyAlloc>;
```

With `Alloc = void` the allocator is type-erased per call; with a concrete `Alloc` the passed allocator must be convertible to it, and a default-constructed `Alloc` is used when no allocator argument is present.

### elements_of

```cpp
template<std::ranges::range Range, typename Alloc = std::allocator<std::byte>>
struct elements_of { Range range; Alloc allocator; };
```

A tag aggregate marking a `co_yield` operand as "yield each element of". The deduction guide binds rvalue ranges by reference (`Range&&`); the operand only needs to live for the duration of the `co_yield` expression's suspension, i.e. until the nested elements are exhausted.

## Example

Adapted from `examples/generator.cpp`:

```cpp
#include <array>
#include <iostream>
#include <ranges>
#include <utility>
#include <coio/generator.h>

auto fibonacci(std::size_t n) -> coio::generator<int> {
    int a = 0, b = 1;
    while (n--) {
        co_yield b;
        a = std::exchange(b, a + b);
    }
}

auto iota(int n) -> coio::generator<int> {
    for (int i = 0; i < n; ++i) co_yield i;
}

template<typename T>
struct Node {
    auto traverse_inorder() const -> coio::generator<const T&> {
        if (left)  co_yield coio::elements_of{left->traverse_inorder()};   // recursion
        co_yield value;
        if (right) co_yield coio::elements_of{right->traverse_inorder()};
    }

    T value;
    Node *left{}, *right{};
};

auto main() -> int {
    for (int x : fibonacci(10)) std::cout << x << ' ';   // 1 1 2 3 5 8 13 21 34 55
    std::cout << '\n';

    for (int x : iota(10)                                 // generators are views
        | std::views::filter([](int i) { return i % 2 == 0; })
        | std::views::transform([](int i) { return i * i; })) {
        std::cout << x << ' ';                            // 0 4 16 36 64
    }
    std::cout << '\n';

    std::array<Node<char>, 7> tree;
    tree = {
        Node{'D', &tree[1], &tree[2]},
        Node{'B', &tree[3], &tree[4]}, Node{'F', &tree[5], &tree[6]},
        Node{'A'}, Node{'C'}, Node{'E'}, Node{'G'},
    };
    for (char c : tree[0].traverse_inorder()) std::cout << c << ' ';  // A B C D E F G
    std::cout << '\n';
}
```

## See also

- [task](task.md) — the asynchronous counterpart
- [Core Concepts](../concepts.md)
