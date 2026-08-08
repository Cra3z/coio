# Miscellaneous Utilities

Small self-contained vocabulary types that coio uses in its own API and ships for general use: string helpers (`zstring_view`, `fixed_string`), containers and smart pointers (`inplace_vector`, `retain_ptr`), RAII helpers (`scope_exit`), sender plumbing (`async_result`), and allocator helpers (`new_object`, `allocator_resource`). None of them depend on the I/O layer.

Headers: one per entity, listed in each section below.

## Overview

| Type | Header | One-liner |
|------|--------|-----------|
| `zstring_view` | `<coio/utils/zstring_view.h>` | `string_view` guaranteed NUL-terminated |
| `fixed_string<N>` | `<coio/utils/fixed_string.h>` | constexpr fixed-size string (NTTP-friendly) |
| `inplace_vector<T, N>` | `<coio/utils/inplace_vector.h>` | fixed-capacity vector, no allocation |
| `retain_ptr<T>` / `retain_base` | `<coio/utils/retain_ptr.h>` | intrusive reference-counted pointer |
| `scope_exit<EF>` | `<coio/utils/scope_exit.h>` | run a callable on scope exit |
| `async_result<...>` | `<coio/utils/async_result.h>` | store a completion now, replay it later |
| `new_object` / `delete_object` | `<coio/utils/new_object.h>` | allocator-based single-object new/delete |
| `allocator_resource` | `<coio/utils/allocator_resource.h>` | wrap any allocator as a `pmr::memory_resource` |

`variant_sender` is documented with the [sender algorithms](algorithms.md#variant_sender).

## API Reference

### zstring_view

Header: `#include <coio/utils/zstring_view.h>`

```cpp
template<typename CharType, typename CharTraits = std::char_traits<CharType>>
class basic_zstring_view; // privately derives from std::basic_string_view

using zstring_view = basic_zstring_view<char>;
```

A read-only view of a **NUL-terminated** character sequence — what to pass to APIs that will hand the string to C functions. Constructible implicitly from a `const CharType*` C string or a `std::basic_string`; **not** constructible from a plain `string_view` (which carries no termination guarantee).

- Exposes the read-only `string_view` interface (`size`, `find`, iteration, comparison, `operator[]`, `starts_with`/`ends_with`, ...), but no `remove_prefix`/`remove_suffix`/`substr` (they would break the invariant).
- `c_str() -> const_pointer` — the NUL-terminated pointer.
- `view() -> std::basic_string_view<...>` — explicit conversion back to a plain view.
- `std::hash` and `std::formatter` support.

### fixed_string

Header: `#include <coio/utils/fixed_string.h>`

```cpp
template<typename CharType, std::size_t N>
class basic_fixed_string;               // N characters + implicit NUL

using fixed_string    = basic_fixed_string<char, N>;      // via CTAD
using fixed_wstring   = basic_fixed_string<wchar_t, N>;
using fixed_u8string  = basic_fixed_string<char8_t, N>;
using fixed_u16string = basic_fixed_string<char16_t, N>;
using fixed_u32string = basic_fixed_string<char32_t, N>;
```

A `constexpr`, structural, fixed-length string — usable as a non-type template parameter. Constructible from a string literal (`basic_fixed_string fs{"hello"};` deduces `N == 5`), from a pointer plus `std::integral_constant<std::size_t, N>`, or from individual characters.

- Full random-access container interface: `data`, `c_str` (NUL-terminated), `operator[]`, `front`/`back`, iterators, `size`/`length`/`empty` (all `static`/`constexpr`).
- `view()` — `std::basic_string_view` over the contents.
- `operator+` concatenation (yields `basic_fixed_string<Char, N + M>`), `==`/`<=>`, stream output, `std::hash`, `std::formatter`.

### inplace_vector

Header: `#include <coio/utils/inplace_vector.h>`

```cpp
template<typename T, std::size_t N>
class inplace_vector;   // N > 0; T must be a movable object type
```

A pre-C++26 implementation of `std::inplace_vector`: a vector with fixed capacity `N` whose elements live inside the object — no heap allocation, usable in `constexpr`. The interface follows the standard proposal:

- Element access (`operator[]`, `at`, `front`, `back`, `data`), iterators, `size`/`empty`, `static` `capacity`/`max_size`.
- Growth: `push_back`/`emplace_back` (**throw** `std::bad_alloc` when full), `try_push_back`/`try_emplace_back` (return `nullptr` when full), `unchecked_*` variants (**precondition**: not full), `append_range`/`try_append_range`.
- `insert`/`insert_range`/`emplace`, `erase`, `resize`, `assign`/`assign_range`, `clear`, `pop_back`, `swap`; free `erase`/`erase_if`.
- `coio::from_range` (`from_range_t`) tag for range construction.

### retain_ptr

Header: `#include <coio/utils/retain_ptr.h>`

```cpp
template<typename T> concept simple_retainable = /* p->retain(); p->lose() noexcept */;
template<typename T> concept retainable       = /* + p->use_count() noexcept -> integral */;

template<typename T>       // T models simple_retainable
class retain_ptr;

template<typename Derived>
class retain_base;         // CRTP helper providing retain/lose/use_count
```

An **intrusive** smart pointer: the pointee owns its reference count and exposes `retain()` / `lose()`; `retain_ptr` merely calls them. Copying retains, destruction loses; when the count reaches zero the object disposes of itself.

- The usual smart-pointer surface: `get`, `operator*`/`->`, `operator bool`, `reset`, `swap`, comparisons, `std::hash`. `use_count()` is available when `T` models the stronger `retainable` concept.
- Converting construction/assignment from `retain_ptr<U>` where `U` derives from `T`.
- `retain_base<Derived>` supplies an atomic count; `Derived` befriends it and implements `do_lose()` (called when the count drops to zero, typically `delete this` or allocator-aware disposal).

### scope_exit

Header: `#include <coio/utils/scope_exit.h>`

```cpp
template<typename EF>
class scope_exit;          // CTAD: scope_exit guard{[]{ ... }};
```

Runs the stored callable when the guard is destroyed — the standard `std::experimental::scope_exit` shape.

- `release()` — dismiss the guard; the callable will not run.
- `reset()` — run the callable now (once); further destruction is a no-op.
- Move-only. If constructing the stored callable throws, the passed-in callable is invoked before the exception propagates (no leak of the cleanup action).

### async_result

Header: `#include <coio/utils/async_result.h>`

```cpp
template<typename, typename> class async_result; // only this specialization exists:

template<typename... Values, typename Error>
class async_result<execution::set_value_t(Values...), execution::set_error_t(Error)>;
```

A small building block for writing custom operations: an object that is *both* a receiver-shaped **storage cell** and a **sender that replays** what was stored.

- Store exactly one completion by calling `set_value(values...)`, `set_error(e)`, or `set_stopped()` on it. **Precondition**: nothing stored yet. A default-constructed `async_result` holds "stopped".
- `forward_to(rcvr)` — deliver the stored completion to a real receiver (`set_value`/`set_error`/`set_stopped` accordingly).
- As a sender: `connect`/`start` delivers the stored completion on start. Completion signatures: `set_value_t(Values...)`, `set_error_t(Error)`, `set_stopped_t()`.

coio's I/O backends use it to capture a completion in one phase (e.g. on the OS completion path) and hand it to the receiver in another.

### new_object / delete_object

Header: `#include <coio/utils/new_object.h>`

```cpp
template<unqualified_object T, simple_allocator Allocator, typename... Args>
[[nodiscard]] auto new_object(const Allocator& allocator, Args&&... args) -> T*;

template<simple_allocator Allocator, typename T>
auto delete_object(const Allocator& allocator, T* ptr) noexcept -> void;
```

Allocator-based counterparts of `new`/`delete` for a single object: rebind the allocator to `T`, allocate, construct (deallocating on constructor throw), and the reverse. The allocator passed to `delete_object` must compare equal to the one used for `new_object`.

### allocator_resource

Header: `#include <coio/utils/allocator_resource.h>`

```cpp
class allocator_resource : public std::pmr::memory_resource {
public:
    template<simple_allocator Alloc>
    explicit allocator_resource(const Alloc& alloc);
    // non-copyable
};
```

Adapts an arbitrary allocator into a `std::pmr::memory_resource`, so allocator-typed customization points can feed `std::pmr::polymorphic_allocator` consumers. Supports alignments up to 256 (`std::bad_alloc` beyond that); small allocator objects are stored inline. Two `allocator_resource`s compare equal when their wrapped allocators do.

!!! warning
    As with any `pmr` setup, the `allocator_resource` must outlive every allocation made through it.

## Example

```cpp
#include <coio/utils/scope_exit.h>
#include <coio/utils/zstring_view.h>
#include <coio/utils/inplace_vector.h>

auto parse_args(coio::zstring_view program) -> void {
    std::FILE* f = std::fopen(program.c_str(), "rb");   // needs NUL-termination
    if (!f) return;
    coio::scope_exit close_file{[&] { std::fclose(f); }};

    coio::inplace_vector<int, 8> first_bytes;           // no heap allocation
    for (int c; first_bytes.size() < first_bytes.capacity()
                and (c = std::fgetc(f)) != EOF; ) {
        first_bytes.unchecked_push_back(c);
    }
    // f closed automatically
}
```

## Internal headers

Everything under `include/coio/detail/` is internal by definition and may change without notice. In `include/coio/utils/`, the following are also considered internal support machinery and are deliberately not documented here: `atomutex.h` (an `atomic_flag`-based blocking mutex used by the primitives), `format.h`, `type_traits.h`, `utility.h`, `stop_token.h`'s helper types other than `stop_when` (`stop_combiner`, `stop_propagator`), and the `allocator_adaptor` helpers in `allocator_resource.h`. `polymorphic_scheduler.h` is documented under [Execution](../execution/polymorphic-scheduler.md).

## See also

- [Waiting & Algorithms](algorithms.md) — `variant_sender`, `stop_when`
- [Buffers & Channels](buffers.md) — `flat_buffer`, `streambuf`, `fifo`
- [Concepts](../concepts.md)
