# coio::generator

**Header:** `<coio/generator.h>`

A synchronous generator with lazy `co_yield`. It is equivalent to [P2502 `std::generator`](https://wg21.link/p2502), but available in C++20.

## Template Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `Ref` | — | The reference type yielded by the generator. |
| `Val` | `void` | The value type. When `void`, `value_type` is `std::remove_cvref_t<Ref>`. |
| `Alloc` | `void` | The allocator type. Must be `void` or a `simple_allocator`. |

## Member Types

| Type | Definition |
|------|-----------|
| `yielded` | `conditional_t<is_reference_v<reference>, reference, const reference&>` |
| `promise_type` | *implementation-defined* |

The generator inherits from `std::ranges::view_interface<generator>`, providing members like `empty()`, `size()` (for sized ranges), `front()`, `back()`, `operator[]`, and more when applicable.

## Member Functions

### Constructors and Destructor

| Name | Description |
|------|-------------|
| `generator(generator&& other) noexcept` | Move constructor. |
| `~generator()` | Destructor. Destroys the coroutine frame. |
| *(copy deleted)* | Generators are move-only. |

### Assignment

| Name | Description |
|------|-------------|
| `operator=(generator other) noexcept` | Move assignment (by-value, swaps). |

### Iteration

| Name | Description |
|------|-------------|
| `begin()` | Begins iteration. Resumes the coroutine and returns an `iterator`. |
| `end() const noexcept` | Returns `std::default_sentinel_t`. |

## Nested Class: `iterator`

Models `std::input_iterator`.

| Member | Description |
|--------|-------------|
| `using value_type = generator::value_type` | |
| `using difference_type = std::ptrdiff_t` | |
| `using iterator_category = std::input_iterator_tag` | |
| `explicit iterator(coroutine_handle<promise_type>) noexcept` | Constructs from a coroutine handle. |
| `iterator(iterator&& other) noexcept` | Move constructor. |
| `operator=(iterator&& other) noexcept` | Move assignment. |
| `operator*() const noexcept` | Returns the current yielded value. |
| `operator++()` | Resumes the coroutine to the next yield point. |
| `operator++(int)` | Post-increment. Returns `void` (input iterator). |
| `friend operator==(const iterator&, std::default_sentinel_t) noexcept` | Sentinel comparison. |

## Example

### Basic Sequence

```cpp
#include <coio/generator.h>
#include <iostream>

auto fibonacci(std::size_t n) -> coio::generator<int> {
    int a = 0, b = 1;
    while (n--) {
        co_yield b;
        a = std::exchange(b, a + b);
    }
}

int main() {
    for (int x : fibonacci(10)) {
        std::cout << x << ' ';
    }
    // Output: 1 1 2 3 5 8 13 21 34 55
}
```

### Recursive Generation with `elements_of`

Use `coio::elements_of` to yield elements from a sub-generator or range:

```cpp
#include <coio/generator.h>
#include <queue>

struct Node {
    int value;
    Node* left = nullptr;
    Node* right = nullptr;
};

// Breadth-first traversal
auto bfs(Node* root) -> coio::generator<const Node*> {
    std::queue<Node*> q;
    q.push(root);
    while (!q.empty()) {
        auto* node = q.front(); q.pop();
        co_yield node;
        if (node->left)  q.push(node->left);
        if (node->right) q.push(node->right);
    }
}
```

## `coio::elements_of`

**Header:** `<coio/generator.h>`

A helper struct used with `co_yield` to yield all elements from a range or generator.

```cpp
template<typename Range, typename Alloc = std::allocator<std::byte>>
struct elements_of {
    Range range;
    Alloc allocator;
};

// Deduction guide
template<typename Range, typename Alloc = std::allocator<std::byte>>
elements_of(Range&&, Alloc = Alloc()) -> elements_of<Range&&, Alloc>;
```

**Example:**

```cpp
auto chain(auto g1, auto g2) -> coio::generator<int> {
    co_yield coio::elements_of{std::move(g1)};
    co_yield coio::elements_of{std::move(g2)};
}
```

## Notes

- The generator is single-pass: you can only iterate through it once.
- After the generator finishes or is destroyed, all iterators are invalidated.
