# when_any / when_any_with_variant / variant_sender

**Header:** `<coio/core.h>`

---

## `coio::when_any`

```cpp
inline constexpr when_any_t when_any{};
```

A sender adaptor that takes multiple senders and completes when **any one** of them completes. All other senders are stopped (cancelled) once one completes.

### Signature

```cpp
template<execution::sender... Sender>
    requires (sizeof...(Sender) > 0)
auto when_any(Sender&&... sndr) -> sender;
```

### Completion

Completes with the same value/error/stopped channels as the first sender to complete. The result type matches that of the winning sender.

### Example

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>

auto job(auto sched, std::string_view name,
         int val, std::chrono::seconds delay) -> coio::task<int> {
    coio::timer t{sched};
    co_await t.async_wait(delay);
    co_return val;
}

int main() {
    coio::time_loop ctx;
    auto result = coio::this_thread::sync_wait_with_variant(
        coio::when_any(
            job(ctx.get_scheduler(), "foo", 114, std::chrono::seconds{2}),
            job(ctx.get_scheduler(), "bar", 514, std::chrono::seconds{1}),
            [&]() -> coio::task<> { ctx.run(); co_return; }()
        )
    ).value();
    // result is a variant containing the first completion
}
```

---

## `coio::when_any_with_variant`

```cpp
inline constexpr when_any_with_variant_t when_any_with_variant{};
```

Wraps `when_any` with `into_variant`, so the winning result is always packaged as a `std::variant`.

### Example

```cpp
auto var = coio::this_thread::sync_wait_with_variant(
    coio::when_any_with_variant(
        job(ctx.get_scheduler(), "fast", 42, 100ms),
        job(ctx.get_scheduler(), "slow", 99, 2000ms),
        [&]() -> coio::task<> { ctx.run(); co_return; }()
    )
).value();

std::visit([](auto&& t) {
    if constexpr (!std::same_as<std::decay_t<decltype(t)>, std::monostate>) {
        auto [val] = t;
        std::println("Result: {}", val);
    }
}, var);
```

---

## `coio::variant_sender`

```cpp
template<typename... Sndrs>
class variant_sender;
```

A sender that wraps a `std::variant<Sndrs...>` and delegates `connect` to whichever sender is active.

### Member Functions

| Name | Description |
|------|-------------|
| `variant_sender(Expr&&)` | Constructs from anything convertible to the internal `variant<Sndrs...>`. |
| `connect(Rcvr) &&` | Connects the active sender to a receiver. |
| `static get_completion_signatures()` | Returns the merged completion signatures of all `Sndrs...`. |

### Completion Signatures

The completion signatures are the merge of all individual sender completion signatures.
