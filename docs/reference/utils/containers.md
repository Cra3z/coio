# Container Utilities

---

## `coio::inplace_vector`

**Header:** `<coio/utils/inplace_vector.h>`

A fixed-capacity vector with storage embedded in the object (no dynamic allocation). Similar to C++26 `std::inplace_vector`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `T` | Element type. |
| `N` | Fixed capacity. |

### Member Functions

#### Observers

| Name | Description |
|------|-------------|
| `begin() / end()` and const/reverse variants. | |
| `size() const -> size_t` | Current number of elements. |
| `static capacity() -> size_t` | Returns `N`. |
| `static max_size() -> size_t` | Returns `N`. |
| `empty() const -> bool` | |
| `front() / back()` | Access. |
| `operator[](size_t) / at(size_t)` | Indexed access. |
| `data() -> T*` | Raw storage. |

#### Modifiers

| Name | Description |
|------|-------------|
| `push_back(T&& / const T&)` | Append one element. |
| `pop_back()` | Remove last. |
| `emplace_back(Args&&...)` | Emplace at end. |
| `try_emplace_back(Args&&...) -> T*` | Emplace or `nullptr` if full. |
| `unchecked_emplace_back(Args&&...)` | Emplace (no capacity check). |
| `insert(pos, value)` | Insert at position. |
| `emplace(pos, Args&&...)` | Emplace at position. |
| `erase(pos) / erase(first, last)` | Remove elements. |
| `clear()` | Remove all. |
| `resize(n) / resize(n, value)` | Resize. |
| `assign(n, value) / assign_range(R&&)` | Assign. |
| `append_range(R&&)` | Append from range. |
| `try_append_range(R&&)` | Append or return `false` if would overflow. |
| `swap(inplace_vector&)` | Swap. |

### Free Functions

```cpp
erase(inplace_vector<T, N>&, const T& value) -> size_t;
erase_if(inplace_vector<T, N>&, Predicate pred) -> size_t;
```

### Example

```cpp
#include <coio/utils/inplace_vector.h>

coio::inplace_vector<int, 16> vec;
vec.push_back(42);
vec.emplace_back(100);
for (int x : vec) {
    std::println("{}", x);
}
```

---

## `coio::basic_fixed_string`

**Header:** `<coio/utils/fixed_string.h>`

A compile-time fixed-size string.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `CharType` | Character type. |
| `N` | Fixed size. |

### Type Aliases

```cpp
template<size_t N> using fixed_string   = basic_fixed_string<char, N>;
template<size_t N> using fixed_wstring  = basic_fixed_string<wchar_t, N>;
template<size_t N> using fixed_u8string = basic_fixed_string<char8_t, N>;
template<size_t N> using fixed_u16string = basic_fixed_string<char16_t, N>;
template<size_t N> using fixed_u32string = basic_fixed_string<char32_t, N>;
```

### Member Functions

| Name | Description |
|------|-------------|
| `begin() / end()` | Iterators. |
| `size() const -> size_t` | String length. |
| `data() const -> const CharType*` | Raw data. |
| `c_str() const -> const CharType*` | Null-terminated pointer. |
| `view() const -> std::basic_string_view<CharType>` | String view. |
| `front() / back()` | Access. |
| `operator[](size_t)` | Indexed access. |
| `operator+` | Concatenation (returns `basic_fixed_string<CharType, N+M>`). |
| `operator== / operator<=>` | Comparison. |

CTAD deduces `N` from string literals.

### Example

```cpp
coio::fixed_string hello = "Hello, ";
coio::fixed_string world = "World!";
auto msg = hello + world;
static_assert(msg.size() == 13);
std::println("{}", msg.view());
```

---

## `coio::basic_zstring_view`

**Header:** `<coio/utils/zstring_view.h>`

A null-terminated string view. Inherits from `std::basic_string_view`.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `CharType` | Character type. |
| `CharTraits` | Character traits (default `std::char_traits<CharType>`). |

### Type Alias

```cpp
using zstring_view = basic_zstring_view<char>;
```

### Member Functions

| Name | Description |
|------|-------------|
| `c_str() const -> const CharType*` | Returns a null-terminated pointer. |
| `view() const -> std::basic_string_view<CharType>` | Returns the underlying view. |

All `string_view` members are exposed via `using` declarations.

### Example

```cpp
void open_file(coio::zstring_view path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    // ...
}

open_file("/etc/hostname");
```
