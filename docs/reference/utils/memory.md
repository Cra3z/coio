# Memory Utilities

---

## `coio::retain_ptr`

**Header:** `<coio/utils/retain_ptr.h>`

An intrusive shared pointer. The pointed-to type must support `retain()` and `lose()`.

### Concepts

```cpp
template<typename T>
concept simple_retainable = requires(T* t) {
    t->retain();
    t->lose();
};

template<typename T>
concept retainable = simple_retainable<T>
    && requires(const T* t) {
        { t->use_count() } -> std::integral;
    };
```

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `T` | Pointee type. Must satisfy `simple_retainable`. |

### Member Functions

| Name | Description |
|------|-------------|
| *(default)*, copy, move constructors. | |
| `operator->() / operator*()` | Dereference. |
| `get() -> T*` | Raw pointer. |
| `use_count() -> size_t` | Reference count (requires `retainable`). |
| `reset() / reset(T*)` | Release / re-assign. |
| `swap(retain_ptr&)` | Swap. |
| `operator== / operator!=` with `nullptr`. | |

### Free Function

```cpp
template<typename T, typename... Args>
auto make_retain(Args&&... args) -> retain_ptr<T>;
```

Allocate and construct with `retain_ptr` ownership (initial ref count = 1).

---

## `coio::retain_base`

**Header:** `<coio/utils/retain_ptr.h>`

A CRTP base class for reference-counted objects.

```cpp
template<typename Derived>
class retain_base;
```

Provides `retain()`, `lose()`, and `use_count()`. The derived class must implement `do_lose()` (called when the ref count reaches zero).

### Example

```cpp
struct MyObject : coio::retain_base<MyObject> {
    void do_lose() override { delete this; }
};

auto ptr = coio::make_retain<MyObject>();
```

---

## `coio::allocator_resource`

**Header:** `<coio/utils/allocator_resource.h>`

Adapts any `simple_allocator` into a `std::pmr::memory_resource`.

```cpp
template<simple_allocator Alloc>
class allocator_resource : public std::pmr::memory_resource { ... };
```

Uses small-object optimization for proxied allocators.

---

## `coio::allocator_adaptor`

**Header:** `<coio/utils/allocator_resource.h>`

A special allocator only for `void`:

```cpp
template<>
struct allocator_adaptor<void> {
    auto get_allocator() const -> std::pmr::polymorphic_allocator<>;
};
```

Adapts any simple allocator to the polymorphic allocator model.
