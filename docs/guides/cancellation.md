# Cancellation Guide

coio supports cooperative cancellation via [stop tokens](https://en.cppreference.com/w/cpp/thread/stop_token) integrated with the sender/receiver model.

## How Cancellation Works

1. A **stop source** is used to request cancellation
2. A **stop token** is obtained from the source
3. The token is attached to senders via `stop_when` or passed to scopes
4. When cancellation is requested, the sender completes with `set_stopped()`

`set_stopped()` is the sender/receiver stopped channel. It is distinct from the error channel and should not be documented as a `std::system_error`.

## stop_when

The fundamental cancellation primitive — attaches a stop token to any sender:

```cpp
#include <coio/utils/stop_token.h>
#include <coio/utils/timer.h>

coio::inplace_stop_source stop_src;

auto cancellable_operation(auto sched) -> coio::task<> {
    coio::timer timer{sched};
    co_await coio::stop_when(
        timer.async_wait(std::chrono::seconds{10}),
        stop_src.get_token()
    );
    std::println("Operation completed normally");
}

// From elsewhere:
stop_src.request_stop();
```

If `stop_src.request_stop()` wins the race, the wrapped sender completes with `set_stopped()` and the awaiting operation is cancelled rather than reported as an error.

## timer Cancellation

`coio::timer` uses `stop_when` internally. Calling `cancel()` requests stop on the internal stop source:

```cpp
auto timer_demo(auto sched) -> coio::task<> {
    coio::timer timer{sched};
    coio::async_scope scope;

    scope.spawn([&timer]() -> coio::task<> {
        co_await timer.async_wait(30s);
        std::println("Should not print if cancelled");
    }());

    co_await coio::timer{sched}.async_wait(1s);
    timer.cancel(); // requests stop for the pending wait
    co_await scope.join();
}
```

## async_scope Cancellation

Call `request_stop()` on a scope to cancel all spawned work:

```cpp
coio::async_scope scope;

scope.spawn(long_running_work(sched, scope.get_token()));

// Later, on signal or timeout:
scope.request_stop();
coio::this_thread::sync_wait(scope.join());
```

Coroutines spawned via `scope.spawn()` should check the stop token periodically:

```cpp
auto long_running_work(auto sched, auto token) -> coio::task<> {
    while (!token.stop_requested()) {
        co_await do_some_work();
    }
}
```

## Socket Cancellation

Call `cancel()` on a socket to abort all pending async operations:

```cpp
auto with_timeout(auto& sock, auto sched) -> coio::task<> {
    coio::inplace_stop_source stop_src;
    char buf[4096];

    // Spawn a timeout coroutine
    coio::async_scope scope;
    scope.spawn([](auto& src, auto sched) -> coio::task<> {
        coio::timer t{sched};
        co_await t.async_wait(5s);
        src.request_stop(); // triggers after 5s
    }(stop_src, sched));

    // Wrap the read with the stop token
    auto n = co_await coio::stop_when(
        sock.async_read_some(coio::as_writable_bytes(buf)),
        stop_src.get_token()
    );
    (void)n; // read completed within 5s
}
```

If the timeout fires first, the wrapped read completes with `set_stopped()`.

Alternatively, call `sock.cancel()` directly:

```cpp
coio::timer t{sched};

scope.spawn([](auto& sock) -> coio::task<> {
    char buf[4096];
    co_await sock.async_read_some(coio::as_writable_bytes(buf));
}(sock));

co_await t.async_wait(5s);
sock.cancel(); // abort the pending read
```

## Stop Token Types

| Token | Description |
|-------|-------------|
| `inplace_stop_source` / `inplace_stop_token` | Lightweight, in-place stop token |
| `never_stop_token` | Can never be stopped. Use to signal "no cancellation" |
| `stop_combiner<Tokens...>` | Combines multiple tokens (logical OR) |
| `stop_propagator<Source, Token>` | Propagates stop from one token to a source |

## Propagation Pattern

Propagate an external stop token to a local stop source:

```cpp
coio::stop_propagator<coio::inplace_stop_source, decltype(external_token)>
    propagator{external_token};

auto local_token = propagator.get_token();
// When external_token fires, local_token also becomes stopped
```

## Best Practices

1. **Pass tokens explicitly** to long-running coroutines
2. **Check `token.stop_requested()`** in loops
3. **Use `stop_when`** to attach external cancellation to any sender
4. **Wrap I/O with timeouts** using `coio::when_any` or `stop_when` + timer
5. **Call `scope.request_stop()`** for graceful shutdown
