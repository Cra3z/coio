# Stop Tokens

**Header:** `<coio/utils/stop_token.h>`

coio provides a family of stop token types for cooperative cancellation, plus `stop_when` for attaching external cancellation to senders.

---

## `coio::inplace_stop_source`

An in-place stop source using an intrusive linked list for callback registration.

| Name | Description |
|------|-------------|
| `stop_requested() const noexcept -> bool` | Returns `true` if stop has been requested. |
| `static constexpr stop_possible() -> bool` | Always returns `true`. |
| `get_token() -> inplace_stop_token` | Returns a token bound to this source. |
| `request_stop() -> bool` | Requests stop. Returns `true` if this was the first request. |

## `coio::inplace_stop_token`

| Name | Description |
|------|-------------|
| `stop_requested() const noexcept -> bool` | Returns `true` if the associated source has been requested to stop. |
| `stop_possible() const noexcept -> bool` | Returns `true`. |
| `using callback_type<Callback> = inplace_stop_callback<Callback>` | |
| `swap(inplace_stop_token&)` | |
| `operator==` / `operator!=` | Equality comparison. |

## `coio::inplace_stop_callback`

```cpp
template<typename Callback>
class inplace_stop_callback;
```

Register a callback that fires when the associated stop source is requested to stop. Automatically unregisters on destruction.

## `coio::never_stop_token`

A token that can never be stopped.

| Name | Description |
|------|-------------|
| `stop_requested() -> false` | Always `false`. |
| `static stop_possible() -> false` | Always `false`. |

## `coio::stop_combiner`

```cpp
template<typename... StopTokens>
class stop_combiner;
```

Combines 2+ stop tokens via logical OR. The inner `callback_type<Callback>` fires once when **any** token requests stop.

## `coio::stop_propagator`

```cpp
template<typename StopSource, typename StopToken>
class stop_propagator;
```

Propagates stop from an external `StopToken` to an `inplace_stop_source`.

---

## `coio::stop_when`

```cpp
auto stop_when(execution::sender auto sndr,
               stoppable_token auto token) -> sender;
```

Attaches an external stop token to a sender. The resulting sender completes with `set_stopped()` when the token fires, or completes normally otherwise.

### Example

```cpp
#include <coio/utils/stop_token.h>
#include <coio/utils/timer.h>

coio::inplace_stop_source stop_src;

auto cancellable_work(auto sched) -> coio::task<> {
    coio::timer t{sched};
    // Wrap the timer with a stop token
    co_await coio::stop_when(
        t.async_wait(std::chrono::seconds{10}),
        stop_src.get_token()
    );
    // If we reach here, the timer fired normally.
    // If stop was requested, the wrapped sender completed on the stopped channel.
}

// Later:
stop_src.request_stop();
```

### How timers use it

`coio::timer::async_wait` applies `stop_when` internally:

```cpp
auto async_wait(duration d) const noexcept {
    return stop_when(
        sched_.schedule_after(d),
        stop_source_.get_token()
    );
}
```
