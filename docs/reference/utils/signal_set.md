# coio::signal_set

**Header:** `<coio/utils/signal_set.h>`

Async signal handling. Wraps OS signals (e.g., `SIGINT`, `SIGTERM`) so you can `co_await` them.

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `signal_set()` | Default constructor. |
| `signal_set(std::initializer_list<int> signals)` | Constructs with a set of signal numbers to listen for. |
| `~signal_set()` | Destructor. Restores previous signal handlers. |

### Modifiers

| Name | Description |
|------|-------------|
| `add(int signal_number) -> void` | Adds a signal to the set. |
| `remove(int signal_number) -> void` | Removes a signal from the set. |
| `cancel() -> void` | Cancels any pending async wait. |
| `clear() -> void` | Removes all signals from the set. |

### Waiting

| Name | Description |
|------|-------------|
| `async_wait()` | Returns a `wait_sender` that completes with the signal number (`int`) when a signal is received. |

### wait_sender

| Member | Description |
|--------|-------------|
| `sender_concept = execution::sender_tag` | Models sender. |
| `completion_signatures = execution::completion_signatures<set_value_t(int)>` | Completes with the signal number. |

## Free Function

```cpp
auto coio::strsignal(int signum) -> std::string_view;
```

Returns a human-readable name for a signal number (e.g., `"SIGINT"`).

## Example

```cpp
#include <coio/utils/signal_set.h>
#include <coio/asyncio/epoll_context.h>

auto signal_watchdog(io_context& ctx) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    int signum = co_await signals.async_wait();
    std::println("Received signal: ({}) {}", signum, coio::strsignal(signum));
    ctx.request_stop();
}

int main() {
    io_context ctx;
    coio::async_scope scope;
    scope.spawn(signal_watchdog(ctx));
    // ... start servers, etc.
    ctx.run();
    coio::this_thread::sync_wait(scope.join());
}
```

## Notes

- The implementation uses `signalfd` on Linux and `SetConsoleCtrlHandler` on Windows.
- Signals are delivered to the I/O event loop, not to signal handler contexts.
