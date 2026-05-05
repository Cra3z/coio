# coio::async_mutex

**Header:** `<coio/sync_primitives.h>`

A lock-free async mutex. Lock acquisition returns a sender that completes when the lock is acquired; unlocking is synchronous.

## Member Types

| Type | Description |
|------|-------------|
| `lock_sender` | The sender type returned by `lock()`. Move-only. |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `async_mutex()` | Default constructor. |
| *(copy deleted)* | Not copyable. |

### Locking

| Name | Description |
|------|-------------|
| `lock() noexcept -> lock_sender` | Returns a sender that completes when the lock is acquired. |
| `lock_guard() noexcept` | Returns a sender that acquires the lock and completes with an `async_unique_lock<async_mutex>`. |
| `try_lock() noexcept -> bool` | Non-blocking attempt. Returns `true` if the lock was acquired. |
| `unlock() -> void` | Releases the lock and resumes the next waiter. Must be called by the lock owner. |

### lock_sender

The sender returned by `lock()`:

| Member | Description |
|--------|-------------|
| `sender_concept = execution::sender_tag` | Models sender. |
| `completion_signatures = execution::completion_signatures<set_value_t()>` | Completes with no value. |

## Example

```cpp
#include <coio/sync_primitives.h>
#include <coio/core.h>

coio::async_mutex mtx;
int counter = 0;

auto worker(int rounds) -> coio::task<> {
    for (int i = 0; i < rounds; ++i) {
        auto lock = co_await mtx.lock_guard();
        ++counter;
    }
}

int main() {
    coio::async_scope scope;
    scope.spawn(worker(100));
    scope.spawn(worker(100));
    coio::this_thread::sync_wait(scope.join());
    assert(counter == 200);
}
```

## `coio::async_unique_lock`

**Header:** `<coio/sync_primitives.h>`

RAII wrapper for async mutexes. Models `basic_async_lockable`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `AsyncMutex` | Must satisfy `basic_async_lockable` or `async_lockable`. |

### Member Types

| Type | Description |
|------|-------------|
| `mutex_type` | `AsyncMutex`. |

### Member Functions

#### Constructors and Destructor

| Name | Description |
|------|-------------|
| `async_unique_lock()` | Default constructor. Creates an unattached lock. |
| `async_unique_lock(mutex_type&, std::adopt_lock_t) noexcept` | Adopts an already-locked mutex. |
| `async_unique_lock(mutex_type&, std::defer_lock_t) noexcept` | Associates with a mutex but does not lock. |
| `async_unique_lock(mutex_type&, std::try_to_lock_t)` | Associates and attempts `try_lock()`. Requires `async_lockable<AsyncMutex>`. |
| `async_unique_lock(async_unique_lock&& other) noexcept` | Move constructor. |
| `~async_unique_lock()` | Destructor. Calls `unlock()` if owning a lock. |

#### Operations

| Name | Description |
|------|-------------|
| `lock()` | Returns a sender that acquires the lock. |
| `try_lock() -> bool` | Non-blocking lock attempt. Requires `async_lockable<AsyncMutex>`. |
| `unlock() -> void` | Releases the lock. |
| `owns_lock() const noexcept -> bool` | Returns whether this lock owns the mutex. |
| `explicit operator bool() const noexcept` | Same as `owns_lock()`. |
| `mutex() noexcept -> mutex_type*` | Returns a pointer to the associated mutex, or `nullptr`. |
| `release() noexcept -> mutex_type*` | Releases ownership without unlocking. |
| `swap(async_unique_lock& other) noexcept` | Swaps two locks. |
| `friend swap(async_unique_lock&, async_unique_lock&) noexcept` | Free swap. |

### Example

```cpp
coio::async_mutex mtx;

coio::async_unique_lock lock{mtx, std::defer_lock};
co_await lock.lock();
// critical section ...
lock.unlock();

// Or with try_lock:
coio::async_unique_lock lock2{mtx, std::try_to_lock};
if (lock2) {
    // lock acquired
}
```
