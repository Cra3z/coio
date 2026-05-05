# Synchronization Primitives

**Header:** `<coio/sync_primitives.h>`

coio provides async synchronization primitives that **suspend coroutines** instead of blocking threads. They are safe to use across coroutines that may run on different threads.

---

## Concepts

### `coio::basic_async_lockable`

```cpp
template<typename Mutex>
concept basic_async_lockable = requires(Mutex&& mtx) {
    { mtx.lock() } -> execution::sender;
    { mtx.unlock() } -> std::same_as<void>;
    requires std::same_as<
        execution::value_types_of_t<decltype(mtx.lock())>,
        std::variant<std::tuple<>>
    >;
};
```

A type satisfying `basic_async_lockable` provides an async `lock()` that returns a sender, and a synchronous `unlock()`.

### `coio::async_lockable`

```cpp
template<typename Mutex>
concept async_lockable = basic_async_lockable<Mutex>
    && requires(Mutex&& mtx) {
        { mtx.try_lock() } -> boolean_testable;
    };
```

Extends `basic_async_lockable` with a non-blocking `try_lock()`.

---

## Provided Types

| Type | Description |
|------|-------------|
| [async_mutex](async_mutex.md) | Lock-free async mutex |
| [async_unique_lock](async_mutex.md) | RAII lock wrapper |
| [async_semaphore](async_semaphore.md) | Async counting semaphore |
| [async_binary_semaphore](async_semaphore.md) | Async binary semaphore (`async_semaphore<1>`) |
| [async_latch](async_latch.md) | Single-use countdown latch |

---

## Thread Safety

All sync primitives are safe to use across coroutines that may run on different threads. Lock acquisition is async (suspends the coroutine), but unlocking and try-lock are synchronous operations.
