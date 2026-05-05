# coio::async_scope

**Header:** `<coio/utils/async_scope.h>`

A scope object for spawning background work, analogous to [P3149 `std::execution::async_scope`](https://wg21.link/p3149).

## Member Types

| Type | Definition |
|------|-----------|
| `token` | `execution::counting_scope::token` |

## Static Members

| Member | Description |
|--------|-------------|
| `static constexpr std::size_t max_associations` | Maximum number of concurrent spawns. |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `async_scope()` | Default constructor. |
| *(copy deleted)* | Not copyable. |
| `~async_scope()` | Destructor. |

### Spawning

| Name | Description |
|------|-------------|
| `spawn(execution::sender auto sndr) noexcept -> void` | Spawns a sender as background work. Errors from the sender cause `std::terminate()` (via `upon_error`). |

### Completion

| Name | Description |
|------|-------------|
| `join() noexcept` | Returns a sender that completes when all spawned work finishes. |

### Cancellation

| Name | Description |
|------|-------------|
| `request_stop() noexcept -> void` | Requests stop on all spawned work. |
| `get_token() noexcept -> token` | Returns a token that can be passed to `spawn()` or nested scopes. |

### Closing

| Name | Description |
|------|-------------|
| `close() noexcept -> void` | Prevents further `spawn()` calls. |

## Example

### Basic Usage

```cpp
#include <coio/utils/async_scope.h>
#include <coio/core.h>
#include <coio/utils/timer.h>

auto worker(auto sched, int id) -> coio::task<> {
    coio::timer t{sched};
    co_await t.async_wait(std::chrono::seconds{1});
    std::println("Worker {} done", id);
}

int main() {
    coio::time_loop ctx;
    coio::async_scope scope;

    for (int i = 0; i < 5; ++i) {
        scope.spawn(worker(ctx.get_scheduler(), i));
    }

    scope.spawn([&ctx]() -> coio::task<> {
        ctx.run(); co_return;
    }());

    coio::this_thread::sync_wait(scope.join());
    std::println("All workers done");
}
```

### With Cancellation

```cpp
coio::async_scope scope;

// Spawn a long-running task that checks the stop token
scope.spawn([](auto token) -> coio::task<> {
    while (true) {
        if (token.stop_requested()) co_return;
        co_await do_some_work();
    }
}(scope.get_token()));

// Later...
scope.request_stop();
coio::this_thread::sync_wait(scope.join());
```

## Notes

- Internally delegates to `std::execution::counting_scope`.
- Errors are not propagated via `join()` — they cause `std::terminate()`. Handle errors within the spawned tasks if you need graceful error recovery.
- `spawn()` is thread-safe and can be called from any thread.
