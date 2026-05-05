# Miscellaneous Utilities

---

## `coio::scope_exit`

**Header:** `<coio/utils/scope_exit.h>`

A RAII scope guard that executes a function on scope exit.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `EF` | Callable type. Must be invocable and destructible, or an lvalue reference. |

### Deduction Guide

```cpp
scope_exit(EF) -> scope_exit<EF>;
```

### Member Functions

| Name | Description |
|------|-------------|
| `scope_exit(Fn&&)` | Registers the cleanup function. |
| `scope_exit(scope_exit&&)` | Move. The source cancels its cleanup. |
| `~scope_exit()` | Calls the function if still active. |
| `release() -> void` | Cancels the cleanup (function won't be called). |

### Example

```cpp
#include <coio/utils/scope_exit.h>

void do_work() {
    auto* ptr = acquire_resource();
    coio::scope_exit guard{[ptr] { release_resource(ptr); }};
    // ... use ptr ...
    // guard calls release_resource on scope exit
}
```

---

## `coio::atomutex`

**Header:** `<coio/utils/atomutex.h>`

A spinlock using `std::atomic_flag`. For cases where a lightweight, non-suspending mutex is needed.

### Member Functions

| Name | Description |
|------|-------------|
| `lock() -> void` | Acquires the lock (spins). |
| `try_lock() -> bool` | Non-blocking attempt. |
| `unlock() -> void` | Releases the lock. |

---

## Other Utilities

### `coio/utils/utility.h`

| Function | Description |
|----------|-------------|
| `coio::unreachable()` | `[[noreturn]]` marker for unreachable code. |
| `coio::to_underlying(E)` | `std::to_underlying` for C++20. |
| `coio::to_signed(T) -> signed` | Cast to signed type. |
| `coio::to_unsigned(T) -> unsigned` | Cast to unsigned type. |
| `coio::forward_like<T>(U&&) -> auto&&` | Forward with const qualification of `T`. |

### `coio/utils/type_traits.h`

| Type | Description |
|------|-------------|
| `type_list<Ts...>` | Compile-time type list with metafunctions: `size`, `find<T>`, `contains<T>`, `at<I>`, `concat`, `push_front`, `push_back`, `pop_front`, `pop_back`, `reverse`, `remove<T>`, `transform<F>`, `replace<T,U>`, `apply<F>`, `unique`, `filter<Pred>`. |

### `coio/utils/format.h`

| Type | Description |
|------|-------------|
| `no_specification_formatter` | Base for `std::formatter` specializations that don't need format specs. |
| `no_specification_wformatter` | Wide-char version. |
