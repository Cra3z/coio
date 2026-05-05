# coio::async_semaphore

**Header:** `<coio/sync_primitives.h>`

An async counting semaphore. Uses `async_mutex` internally for waiter list synchronization.

## Template Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `LeastMaxValue` | *platform max for atomic signed integer* | The maximum supported count. |

## Member Types

| Type | Description |
|------|-------------|
| `count_type` | `std::atomic_signed_lock_free::value_type` |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `explicit async_semaphore(count_type init) noexcept` | Constructs with the given initial count. |
| *(copy deleted)* | Not copyable. |

### Operations

| Name | Description |
|------|-------------|
| `static constexpr max() noexcept -> count_type` | Returns the maximum supported count. |
| `acquire() noexcept` | Returns a sender that completes when the semaphore is acquired (count decremented). |
| `try_acquire() noexcept -> bool` | Non-blocking acquire. Returns `true` if acquired. |
| `release() noexcept` | Returns a sender that increments the count and resumes waiters. |
| `count() const noexcept -> count_type` | Returns the current count (relaxed load). |

## Completion Signatures

| Operation | Completion |
|-----------|----------|
| `acquire()` | `set_value()` |
| `release()` | `set_value()` |

## Example

```cpp
#include <coio/sync_primitives.h>
#include <coio/core.h>

auto worker(coio::async_semaphore<>& sem, int id) -> coio::task<> {
    co_await sem.acquire();
    std::println("Worker {} acquired semaphore", id);
    // ... do work ...
    co_await sem.release();
}

int main() {
    coio::async_semaphore<> sem{3}; // max 3 concurrent workers
    coio::async_scope scope;
    for (int i = 0; i < 10; ++i) {
        scope.spawn(worker(sem, i));
    }
    coio::this_thread::sync_wait(scope.join());
}
```

## `coio::async_binary_semaphore`

```cpp
using async_binary_semaphore = async_semaphore<1>;
```

An alias for a semaphore with a maximum value of 1. Useful for signaling between coroutines.
