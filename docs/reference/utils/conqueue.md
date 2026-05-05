# Async Queues

**Header:** `<coio/utils/conqueue.h>`

---

## `coio::conqueue`

A multi-producer, multi-consumer async bounded queue.

### Template Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `T` | — | Element type. |
| `Alloc` | `std::allocator<T>` | Allocator type. |
| `Container` | `std::deque<T, Alloc>` | Underlying container. |

### Member Types

| Type | Description |
|------|-------------|
| `value_type` | `T` |
| `allocator_type` | `Alloc` |
| `size_type` | Container's size type |

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `conqueue()` | Default constructor. |
| `explicit conqueue(size_type capacity)` | Constructs with a maximum capacity. |
| *(move-only)* | Move constructible and move assignable. |

#### Observers

| Name | Description |
|------|-------------|
| `empty() const -> bool` | Returns `true` if the queue is empty. |
| `size() const -> size_type` | Returns the current number of elements. |
| `capacity() const -> size_type` | Returns the current capacity. |
| `static max_capacity() -> size_type` | Returns the maximum supported capacity. |
| `get_allocator() -> allocator_type` | Returns the allocator. |

#### Operations

| Name | Description |
|------|-------------|
| `push(T value)` | Returns `coio::task<void, Alloc>`. Completes when the value has been pushed. |
| `emplace(Args&&... args)` | Returns `coio::task<void, Alloc>`. Emplaces and completes when pushed. |
| `emplace(std::allocator_arg_t, Alloc, Args&&...)` | Emplace with allocator. |
| `pop()` | Returns `coio::task<T, Alloc>`. Completes with a popped value. |
| `pop(std::allocator_arg_t, Alloc)` | Pop with allocator. |
| `try_pop()` | Returns `coio::task<std::optional<T>, Alloc>`. Non-blocking pop (returns `nullopt` if empty). |

All `push`/`emplace` operations suspend the caller until there is space. All `pop` operations suspend until there is an element.

### Example

```cpp
#include <coio/utils/conqueue.h>
#include <coio/core.h>

auto producer(coio::conqueue<int>& q, int n) -> coio::task<> {
    for (int i = 0; i < n; ++i) {
        co_await q.push(i);
    }
}

auto consumer(coio::conqueue<int>& q, int expected) -> coio::task<> {
    for (int i = 0; i < expected; ++i) {
        int val = co_await q.pop();
        std::println("Got: {}", val);
    }
}

int main() {
    coio::conqueue<int> q{10}; // capacity 10
    coio::async_scope scope;
    scope.spawn(producer(q, 100));
    scope.spawn(consumer(q, 100));
    coio::this_thread::sync_wait(scope.join());
}
```

---

## `coio::ring_buffer`

A single-producer, single-consumer (SPSC) bounded queue.

### Template Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `T` | — | Element type. |
| `Container` | `std::vector<T>` | Underlying contiguous container. |

### Member Functions

| Name | Description |
|------|-------------|
| `explicit ring_buffer(size_t capacity)` | Constructs with fixed capacity. |
| `empty() const / full() const -> bool` | Status checks. |
| `size() const / capacity() const -> size_t` | Size/capacity. |
| `static max_capacity() -> size_t` | Max supported capacity. |
| `push(T value) -> coio::task<void>` | Push (suspend if full). |
| `emplace(Args&&...) -> coio::task<void>` | Emplace. |
| `pop() -> coio::task<T>` | Pop (suspend if empty). |
| `try_pop() -> coio::task<std::optional<T>>` | Non-blocking pop. |

### `inplace_ring_buffer`

```cpp
template<typename T, std::size_t N>
using inplace_ring_buffer = ring_buffer<T, inplace_vector<T, N>>;
```

A ring buffer with compile-time fixed capacity (no dynamic allocation).

## Thread Safety

| Type | Producers | Consumers | Thread Safety |
|------|-----------|-----------|---------------|
| `conqueue` | Multiple | Multiple | Yes |
| `ring_buffer` | Single | Single | SPSC only |
