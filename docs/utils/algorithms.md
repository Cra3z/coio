# Waiting & Algorithms

coio ships a small set of sender algorithms that complement the standard `std::execution` vocabulary: blocking waits (`coio::this_thread::sync_wait`), first-of-many racing (`when_any`), external cancellation attachment (`stop_when`), and a sender that holds one of several alternative sender types (`variant_sender`). All of them compose freely with `then`, `when_all`, `continues_on`, and the rest of `std::execution`.

Header: `#include <coio/core.h>`
(individually: `<coio/utils/when_any.h>`, `<coio/utils/stop_token.h>`, `<coio/utils/variant_sender.h>`)

## Overview

| Facility | Kind | Purpose |
|----------|------|---------|
| `this_thread::sync_wait(sndr)` | blocking function | run a sender to completion on the current thread, return its value |
| `this_thread::sync_wait_with_variant(sndr)` | blocking function | as above, for senders with multiple value completions |
| `when_any(sndrs...)` | sender algorithm | race senders; first completion wins, losers are cancelled |
| `when_any_with_variant(sndrs...)` | sender algorithm | `when_any` with the result packed into a variant |
| `stop_when(sndr, token)` | sender algorithm | attach an external stop token to a sender |
| `variant_sender<Sndrs...>` | sender type | holds exactly one of several alternative senders |

`sync_wait` and `sync_wait_with_variant` are the standard `std::execution` facilities, re-exported into `coio::this_thread` from the configured backend implementation (stdexec, beman.execution, or `<execution>`). `when_any`, `stop_when`, and `variant_sender` are coio-specific.

## Synopsis

```cpp
namespace coio::this_thread {
    // re-exported from the std::execution implementation
    auto sync_wait(sender auto&& sndr)
        -> std::optional<std::tuple<Values...>>;               // decayed values
    auto sync_wait_with_variant(sender auto&& sndr)
        -> std::optional<std::variant<std::tuple<Ts...>...>>;
}

namespace coio {
    struct when_any_t {
        template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
        auto operator()(Sender&&... sndr) const -> sender-of-first-result;
    };
    inline constexpr when_any_t when_any{};

    struct when_any_with_variant_t {
        template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
        auto operator()(Sender&&... sndr) const; // into_variant(when_any(sndr...))
    };
    inline constexpr when_any_with_variant_t when_any_with_variant{};

    struct stop_when_t {
        template<execution::sender Sndr, stoppable_token StopToken>
        auto operator()(Sndr sndr, StopToken stop_token) const noexcept;
    };
    inline constexpr stop_when_t stop_when{};

    template<typename... Sndrs>
    class variant_sender {
    public:
        using sender_concept = execution::sender_tag;

        template<typename Expr>
            requires std::convertible_to<Expr, std::variant<Sndrs...>>
        variant_sender(Expr&& sndr);

        template<typename Rcvr>
        auto connect(Rcvr rcvr) && noexcept;
        // completion signatures: merged signatures of all Sndrs
    };
}
```

## API Reference

### this_thread::sync_wait

```cpp
auto coio::this_thread::sync_wait(sender auto&& sndr)
    -> std::optional<std::tuple<Values...>>;
```

Blocks the current thread until `sndr` completes, driving an internal `run_loop` so that work delegated to the caller can make progress. The sender must have exactly one value completion signature.

- **Returns**: an engaged `optional` holding the (decayed) values on `set_value`; `std::nullopt` if the sender completed with `set_stopped`.
- **Throws**: on `set_error` — an `std::exception_ptr` error is rethrown, an `std::error_code` error is thrown as `std::system_error`, any other error object is thrown as-is.

```cpp
auto [i, j] = coio::this_thread::sync_wait(coio::when_all(a, b)).value();
```

### this_thread::sync_wait_with_variant

```cpp
auto coio::this_thread::sync_wait_with_variant(sender auto&& sndr)
    -> std::optional<std::variant<std::tuple<Ts...>...>>;
```

Like `sync_wait`, but accepts senders with any number of value completion signatures; the result is the variant of decayed tuples, one alternative per value signature. Blocking, return-on-stopped, and throw-on-error behavior are identical to `sync_wait`.

### when_any

```cpp
template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
auto coio::when_any(Sender&&... sndr) -> sender;
```

Connects and starts all child senders. The **first** child to complete — with `set_value`, `set_error`, *or* `set_stopped` — decides the result and immediately issues a stop request to the remaining children. The composite operation completes only after **all** children have completed, then reproduces the winner's completion.

- **Completion signatures**: the merged (deduplicated) completion signatures of all children. When multiple children have different value signatures, consume the result with `when_any_with_variant` or `sync_wait_with_variant`.
- **Cancellation of losers**: cooperative, via a stop token owned by the `when_any` operation. Children that do not support cancellation simply run to completion before the composite completes.
- **Stopped winner**: if the first completion is `set_stopped`, the composite completes with `set_stopped`.

!!! note
    Each child observes the `when_any`-internal stop token as its environment's stop token; other environment queries are forwarded to the outer receiver unchanged. An external stop request on the *surrounding* operation is **not** forwarded into a running `when_any` automatically — wrap the composite with [`stop_when`](#stop_when) if you need that.

### when_any_with_variant

```cpp
template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
auto coio::when_any_with_variant(Sender&&... sndr) -> sender;
```

Equivalent to `execution::into_variant(when_any(sndr...))`: the racing semantics above, with the winner's values delivered as a single `std::variant<std::tuple<...>...>` value.

### stop_when

```cpp
template<execution::sender Sndr, stoppable_token StopToken>
auto coio::stop_when(Sndr sndr, StopToken stop_token) noexcept -> sender;
```

Returns a sender that behaves like `sndr`, except that the child operation observes a **combined** stop token: a stop request on either `stop_token` or the surrounding operation's own stop token is visible to the child.

- **Completion signatures**: identical to `Sndr` — `stop_when` adds nothing.
- If `StopToken` is an `unstoppable_token`, `stop_when` returns `sndr` unchanged.
- A stop request is a *request*: per the library-wide cancellation contract, it never overwrites a real result. The operation completes with `set_stopped` only if the cancellation path wins; an already-produced value or error is delivered normally.
- `stop_when` has no effect on senders that ignore their stop token.

`coio::timer` is implemented in terms of `stop_when` (see [timer](timer.md)).

### variant_sender

```cpp
template<typename... Sndrs>
class coio::variant_sender;
```

A sender holding exactly one of the alternative sender types `Sndrs...`, chosen at runtime. Implicitly constructible from anything convertible to `std::variant<Sndrs...>` — in particular from any single alternative. Its completion signatures are the merged signatures of all alternatives.

Use it to return different sender types from the branches of one function:

```cpp
auto load(bool cached)
    -> coio::variant_sender<cache_sender, file_sender> {
    if (cached) return cache_hit();   // cache_sender
    return read_from_disk();          // file_sender
}
```

## Example

Racing three timed jobs; the fastest wins and the others are cancelled (from `examples/when_any.cpp`):

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>

auto job(coio::time_loop::scheduler sched, std::string_view name,
         int value, std::chrono::seconds timeout) -> coio::task<int> {
    coio::timer timer{sched};
    co_await timer.async_wait(timeout);
    std::println("{} completed", name);
    co_return value;
}

auto main() -> int {
    using namespace std::chrono_literals;
    coio::time_loop context;
    auto value = coio::this_thread::sync_wait_with_variant(coio::when_any(
        coio::starts_on(context.get_scheduler(), job(context.get_scheduler(), "foo", 114, 2s)),
        coio::starts_on(context.get_scheduler(), job(context.get_scheduler(), "bar", 514, 1s)),
        coio::starts_on(context.get_scheduler(), job(context.get_scheduler(), "qux", 1919, 3s)),
        [&context]() -> coio::task<> {
            context.run();
            co_return;
        }()
    )).value();
    auto [i] = std::get<std::tuple<int>>(value);
    std::println("result: i = {}", i); // result: i = 514, after ~1s
}
```

## See also

- [Concepts](../concepts.md) — senders, receivers, and the cancellation contract
- [Execution contexts](../execution/contexts.md) — where scheduled work completes
- [timer](timer.md) — `stop_when` in action
- [signal_wait](signal-wait.md) — built on `when_any`
- [task](../coroutines/task.md) — awaiting senders inside coroutines
