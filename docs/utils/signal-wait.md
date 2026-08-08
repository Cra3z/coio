# signal_wait

`coio::signal_wait` turns delivery of a process signal (`SIGINT`, `SIGTERM`, ...) into a sender completion, so signal handling composes with the rest of your async code — awaited in a task, raced with `when_any`, cancelled with a stop token — instead of living in a raw signal handler.

Header: `#include <coio/utils/signal_wait.h>`

## Overview

`signal_wait` is a set of free functions returning senders. A single-signal wait completes with the signal number when that signal is delivered; the multi-signal overload races several waits with [`when_any`](algorithms.md#when_any) and completes with whichever signal arrives first. Internally the library installs a process signal handler for each signal that currently has waiters and runs a dedicated broker thread that dispatches deliveries to waiters.

Delivery is **one-shot and sticky** per signal: when a signal fires, all current waiters for it complete and the handler for that signal is restored to `SIG_DFL`. The delivery is remembered — once a signal has fired, subsequent waits for it complete immediately with that signal number; the handler is not reinstalled.

## Synopsis

```cpp
namespace coio {
    struct signal_wait_sender {
        using sender_concept = execution::sender_tag;
        using completion_signatures = execution::completion_signatures<
            execution::set_value_t(int),          // the delivered signal number
            execution::set_error_t(std::error_code),
            execution::set_stopped_t()
        >;

        explicit signal_wait_sender(int signum) noexcept;
        // move-only
    };

    [[nodiscard]] auto signal_wait(int signal_number) noexcept;   // sender

    template<std::convertible_to<int>... SignalNumbers>
    [[nodiscard]] auto signal_wait(SignalNumbers... signal_numbers) noexcept; // sender

    [[nodiscard]] auto strsignal(int signum) noexcept -> std::string_view;
}
```

## API Reference

### signal_wait (single signal)

```cpp
[[nodiscard]] auto signal_wait(int signal_number) noexcept; // sender
```

Returns a sender that registers a waiter for `signal_number` when started.

- **`set_value(int)`** — the signal was delivered; the value is the signal number.
- **`set_error(std::error_code)`** — registration failed: `signal_number` is invalid or unsupported on this platform, or installing the handler failed. The error is a `std::system_category()` code.
- **`set_stopped()`** — the operation's stop token was triggered before delivery; the waiter is unregistered. If removing the waiter leaves a signal with no waiters, its handler is restored to `SIG_DFL`.

The wait is lazy: nothing is registered until the operation is started.

### signal_wait (multiple signals)

```cpp
template<std::convertible_to<int>... SignalNumbers>
[[nodiscard]] auto signal_wait(SignalNumbers... signal_numbers) noexcept; // sender
```

Equivalent to `when_any(signal_wait_sender{s}...)`: waits on every listed signal simultaneously and completes with `set_value(int)` for the **first** one delivered, cancelling the remaining waits. Error and stopped completions follow `when_any` semantics (first completion wins).

### signal_wait_sender

The underlying single-signal sender type (move-only). `signal_wait(int)` wraps it with library plumbing; prefer the free function.

### strsignal

```cpp
[[nodiscard]] auto strsignal(int signum) noexcept -> std::string_view;
```

Returns a human-readable description of a signal number (e.g. `"Interrupt"` for `SIGINT`). Portable replacement for POSIX `::strsignal`.

## Platform differences

| | POSIX (Linux) | Windows |
|---|---|---|
| Supported signals | any valid signal number in `[0, NSIG)` | only `SIGABRT`, `SIGFPE`, `SIGILL`, `SIGINT`, `SIGSEGV`, `SIGTERM` (CRT `signal()`) |
| Handler mechanism | `sigaction`, installed while waiters exist | CRT `signal()`, installed while waiters exist |
| Console Ctrl+C | delivered as `SIGINT` | delivered as `SIGINT` through the CRT's console handling |
| Unsupported signal | `set_error` (`EINVAL`) | `set_error` (`EINVAL`) for any signal outside the list above |

While a `signal_wait` is registered for a signal, the library's handler **replaces** any previously installed handler for it; when the last waiter goes away (completion or cancellation), the disposition is restored to `SIG_DFL` — not to the previous handler. Once a signal has fired, later waits for it complete immediately from the recorded delivery and do not reinstall a handler. Avoid mixing `signal_wait` with other signal-handling machinery for the same signals.

## Completion thread

`signal_wait` completes on the receiver's **start-scheduler**; if the receiver has no start-scheduler, it completes on an unspecified thread. Awaited inside a `coio::task` with an associated scheduler, code after `co_await coio::signal_wait(...)` therefore runs on the task's scheduler as usual.

## Example

Blocking a main thread until Ctrl+C or a termination request:

```cpp
#include <csignal>
#include <coio/core.h>
#include <coio/utils/signal_wait.h>

auto main() -> int {
    std::println("running; press Ctrl+C to quit...");
    auto [signum] = coio::this_thread::sync_wait(
        coio::signal_wait(SIGINT, SIGTERM)
    ).value();
    std::println("got signal: {} ({})", signum, coio::strsignal(signum));
}
```

A typical server shutdown: race the accept loop against a signal with `when_any`, or spawn a watcher task that calls `context.request_stop()` after the signal arrives.

## See also

- [Waiting & Algorithms](algorithms.md) — `when_any`, `stop_when`, `sync_wait`
- [Execution contexts](../execution/contexts.md) — completion-thread guarantees for context work
- [Thread safety](../thread-safety.md) — where completions run, in context
