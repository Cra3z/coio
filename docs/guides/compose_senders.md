# Composing Senders Guide

Senders are the core composability mechanism in coio. This guide covers common composition patterns.

## Pipe Syntax

The most readable way to chain senders is with `|`:

```cpp
auto work = sched.schedule()
    | coio::then([] { return 42; })
    | coio::then([](int x) { return x * 2; })
    | coio::then([](int x) { std::println("Result: {}", x); });

co_await work;
```

## Core Composition Operations

### then — Transform Values

```cpp
int result = co_await(
    coio::just(21)
    | coio::then([](int x) { return x * 2; })
);
// result = 42
```

### let_value — Chain Senders

When you need to start a new async operation based on a value:

```cpp
char buf[4096];

co_await(
    coio::just()
    | coio::let_value([&] {
        return sock.async_read_some(coio::as_writable_bytes(buf));
    })
    | coio::then([](size_t n) { std::println("Read {} bytes", n); })
);
```

### upon_error / upon_stopped — Handle Non-value Channels

```cpp
auto robust_work =
    do_something()
    | coio::upon_error([](std::exception_ptr e) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& ex) {
            std::println("Error: {}", ex.what());
            return 0; // recover with default value
        }
    })
    | coio::upon_stopped([] { return -1; });
```

### when_all — Wait for All

```cpp
auto [r1, r2] = co_await coio::when_all(
    compute_x(),
    compute_y()
);
// Both complete
```

### continues_on — Transfer Execution

Switch schedulers mid-operation:

```cpp
co_await(
    io_sched.schedule()            // start on I/O thread
    | coio::then(do_io_processing)
    | coio::continues_on(cpu_sched) // continue on CPU thread
    | coio::then(do_cpu_processing)
);
```

### starts_on — Launch on a Scheduler

```cpp
auto work = coio::starts_on(cpu_sched,
    coio::just()
    | coio::then(heavy_computation)
);
```

### on — Run Entire Chain on a Scheduler

```cpp
auto work = coio::on(io_sched,
    coio::just()
    | coio::then(io_bound_work)
    | coio::then(more_io_bound_work)
);
```

## when_any — First to Complete

Complete when the first sender finishes:

```cpp
auto result_var = coio::this_thread::sync_wait_with_variant(
    coio::when_any_with_variant(
        fast_operation(),
        slow_operation(),
        timeout(5s)
    )
).value();

std::visit([](auto&& tuple) {
    // handle result from whichever completed first
}, result_var);
```

## Mixing Coroutines and Senders

You can freely mix both styles:

```cpp
// Start with a sender, continue in coroutine
auto n = co_await(
    sched.schedule()
    | coio::then([]{ return 42; })
);

// Use coroutine result in a sender chain
auto work = my_async_function()
    | coio::then([](int result) { return result * 2; });
```

### From sender chain to coroutine

```cpp
auto do_work(auto& sock) -> coio::task<> {
    char buf[1024];

    // Await a sender directly
    auto n = co_await sock.async_read_some(coio::as_writable_bytes(buf));

    // Use in when_all for concurrency
    coio::timer timer{sock.get_io_scheduler()};
    char fill_buf[4096];
    auto [read_n] = co_await coio::when_all(
        coio::async_read(sock, coio::as_writable_bytes(fill_buf)),
        timer.async_wait(5s)
    );
}
```

### From coroutine to sender chain

```cpp
auto caller() -> coio::task<int> {
    co_return 42;
}

// Use a task as a sender in a chain:
auto work = caller()
    | coio::then([](int x) { return x * 2; });
```

## Common Patterns

### Read with Timeout

```cpp
auto read_with_timeout(auto& sock, auto sched) -> coio::task<size_t> {
    coio::timer t{sched};
    char buf[4096];
    auto result = co_await coio::when_any_with_variant(
        sock.async_read_some(coio::as_writable_bytes(buf)),
        t.async_wait(5s) | coio::then([] { return size_t{0}; })
    );
    // Handle the variant result...
}
```

### Sequential + Parallel Mix

```cpp
auto step1 = do_step1();
auto [step2, step3] = co_await coio::when_all(
    do_step2(),
    do_step3()
);
auto result = co_await do_step4(step2, step3);
```

### spawn + join

```cpp
coio::async_scope scope;

// Can spawn senders directly
scope.spawn(sched.schedule() | coio::then([]{ do_work(); }));

// Or spawn tasks
scope.spawn(my_task());

// Wait for everything
co_await scope.join();
```

## Error Propagation in Composed Senders

When using `co_await` inside a `task`, errors from the error channel are re-thrown as exceptions:

```cpp
auto safe_work() -> coio::task<> {
    try {
        co_await some_sender();
    } catch (const std::system_error& e) {
        // handle I/O errors
    }
}
```

For raw sender code (not `co_await`), use `upon_error`:

```cpp
auto safe_sender = some_sender()
    | coio::upon_error([](auto e) { /* handle */ });
```
