# Core Algorithms

**Header:** `<coio/core.h>`

coio re-exports the standard `std::execution` CPOs and adds a few custom algorithms.

---

## Re-exported CPOs

These are re-exported into `namespace coio::execution` from the underlying sender implementation:

### Sender Factories

| CPO | Description |
|-----|-------------|
| `just(values...)` | Creates a sender that immediately completes with the given values. |
| `just_error(error)` | Creates a sender that immediately completes with an error. |
| `just_stopped()` | Creates a sender that immediately completes as stopped. |
| `schedule(scheduler)` | Returns a sender that completes when run on the given scheduler. |

### Sender Adaptors

| CPO | Description |
|-----|-------------|
| `then(sender, fn)` | Transforms the value channel. |
| `upon_error(sender, fn)` | Transforms the error channel. |
| `upon_stopped(sender, fn)` | Transforms the stopped channel. |
| `let_value(sender, fn)` | Binds the value channel to a new sender. |
| `let_error(sender, fn)` | Binds the error channel to a new sender. |
| `let_stopped(sender, fn)` | Binds the stopped channel to a new sender. |
| `continues_on(sender, scheduler)` | Transfers execution to a scheduler. |
| `starts_on(scheduler, sender)` | Starts a sender on a given scheduler. |
| `on(scheduler, sender)` | Runs an entire sender on a scheduler. |
| `when_all(senders...)` | Waits for all senders to complete with `set_value`. |
| `when_all_with_variant(senders...)` | Variant form of `when_all`. |
| `into_variant(sender)` | Packs value types into a `std::variant`. |
| `stopped_as_error(sender)` | Converts the stopped channel to an error. |
| `stopped_as_optional(sender)` | Converts the stopped channel to an empty optional. |

### Consuming

| CPO | Description |
|-----|-------------|
| `spawn(sender, scope_token)` | Fire-and-forget. Launches a sender. |
| `spawn_future(sender)` | Spawns a sender, returns a future. |

### Environment

| CPO | Description |
|-----|-------------|
| `read_env(query)` | Reads a value from the receiver's environment. |
| `write_env(sender, ...)` | Writes values into the environment. |

---

## coio-specific Algorithms

### `coio::when_any`

Completes when the first input sender completes. All other senders are stopped.

```cpp
inline constexpr when_any_t when_any{};
auto sndr = coio::when_any(s1, s2, s3);
```

See [when_any](when_any.md) for details.

### `coio::when_any_with_variant`

Wraps `when_any` with `into_variant`, packaging the winning result into a `std::variant`.

```cpp
inline constexpr when_any_with_variant_t when_any_with_variant{};
auto sndr = coio::when_any_with_variant(s1, s2);
```

### `coio::variant_sender`

A type-erased sender over a `std::variant<Sndrs...>`.

```cpp
template<typename... Sndrs>
class variant_sender {
    // models execution::sender
};
```

### `coio::sync_wait`

Synchronous waiting. See below.

---

## `coio::this_thread::sync_wait`

**Header:** `<coio/core.h>`

```cpp
namespace coio::this_thread {
    auto sync_wait(execution::sender auto sndr);
    auto sync_wait_with_variant(execution::sender auto sndr);
}
```

Blocks the calling thread until the sender completes.

| Function | Behavior |
|----------|----------|
| `sync_wait(sender)` | Waits for completion. Returns the value or throws the error. |
| `sync_wait_with_variant(sender)` | Waits for completion. Returns a `std::variant` containing either the value tuple or error. |

`sync_wait` is the bridge between synchronous code and async senders:

```cpp
// From main() or any non-coroutine code:
int result = coio::this_thread::sync_wait(
    sched.schedule() | coio::then([] { return 42; })
);
assert(result == 42);
```
