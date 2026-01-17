# API Reference

This is the **detailed** reference for coio's public headers in `include/coio/`. It focuses on the observable behavior and contracts in the headers (no speculation about unimplemented platforms).

> **Platform note**: Async I/O and networking backends are currently implemented only on Linux (epoll/io_uring). Windows networking/async-IO is **not** implemented yet.

---

## Conventions

### Awaitables and result types

- Most asynchronous operations return an **awaitable** object that can be used with `co_await`.
- Many helpers return `coio::task<T>` or another awaitable wrapper.

### Stop tokens

- Async I/O operations commonly accept a `StopToken` parameter (default `never_stop_token`).
- If the awaiting coroutine supports stoppable semantics, stopping typically completes the operation by invoking the promise’s `unhandled_stopped` path.

### Error handling

- Synchronous socket operations throw `std::system_error` on failure.
- Async socket operations throw `std::system_error` on await-resume when the OS reports errors.
- `coio::error::misc_errc` includes library-specific codes like `eof`.

---

## Coroutine Types

### `coio::task<T, Alloc>`
Header: `#include <coio/task.h>`

**A lazily-started, move-only coroutine.**

**Key properties**

- Move-only; copying is disabled.
- `operator co_await()` starts/resumes the coroutine.
- Destructor destroys the coroutine handle if still owned.

**Typical use**

```cpp
auto foo() -> coio::task<int> {
	co_return 42;
}

int i = co_await foo(); // i = 42
```

### `coio::shared_task<T, Alloc>`
Header: `#include <coio/task.h>`

**A lazily-started, reference-counted coroutine.**

**Key properties**

- Copyable; multiple awaiters can observe the same result.
- Awaiting registers a continuation; all awaiters are resumed on completion.

**Typical use**

```cpp
auto expensive() -> coio::shared_task<int> {
	co_return 42;
}

auto s = expensive();
int a = co_await s;
int b = co_await s; // same result, no recomputation
```

### `coio::generator<Ref, Val, Alloc>`
Header: `#include <coio/generator.h>`

**A synchronous generator** with lazy `co_yield`.

**Key properties**

- Models `std::ranges::view_interface`.
- Supports `elements_of` for recursive generation.
- Exceptions propagate on iteration.

**Typical use**

```cpp
auto fibonacci(std::size_t n) -> coio::generator<int> {
    int a = 0, b = 1;
    while (n--) {
        co_yield b;
        a = std::exchange(b, a + b);
    }
}
// Produces: 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, ...
```

---

## Execution Contexts

### `coio::run_loop`
Header: `#include <coio/execution_context.h>`

Execution context holding a thread-safe **MPSC** task queue and a manually-driven event loop.

**Key APIs**

- `get_scheduler()` → returns a scheduler usable with `schedule(...)`.
- `run()` / `run_one()` → blocking execution of ready work.
- `poll()` / `poll_one()` → non-blocking execution.
- `request_stop()` → ask the loop to stop.

**Notes**

- The loop stops when `work_count_` reaches zero unless new work is added.

### `coio::epoll_context` (Linux only)
Header: `#include <coio/asyncio/epoll_context.h>`

Execution context backed by **epoll**, which is samiliar with `run_loop`, but provides `schedule_io(...)` via its scheduler for socket I/O.

### `coio::uring_context` (Linux only)
Header: `#include <coio/asyncio/uring_context.h>`

Execution context backed by **io_uring**, which is samiliar with `run_loop`, but provides `schedule_io(...)` via its scheduler for socket I/O.

### `coio::work_guard<ExecutionContext>`
Header: `#include <coio/execution_context.h>`

RAII guard that increments outstanding work count while alive.

```cpp
coio::run_loop loop;
auto guard = coio::work_guard{loop};
```

---

## Scheduler Concepts and CPOs
Header: `#include <coio/schedulers.h>`

### Concepts

- `scheduler<T>`: must provide `schedule()` returning an awaitable.
- `timed_scheduler<T>`: additionally provides `now()`, `schedule_after()`, `schedule_at()`.
- `io_scheduler<T>`: scheduler that supports I/O scheduling.

### CPOs

- `schedule(sched)` → awaitable that completes on the scheduler.
- `starts_on(sched, awaitable)` → starts `awaitable` on `sched`.
- `continues_on(awaitable, sched)` → resumes on `sched` after awaitable completes.
- `on(sched, awaitable)` → execute entirely on `sched`.

---

## Core Coroutine Utilities
Header: `#include <coio/core.h>`

### `when_all(awaitables...)`

Await multiple awaitables concurrently; returns an awaitable producing a tuple of results.

### `sync_wait(awaitable)`

Synchronously wait for an awaitable. Returns `optional_t<result>`. If the awaitable completes via **stopped**, the optional is empty.

### `then(awaitable, fn)`

Compose a continuation. If the awaitable returns `T`, `fn(T)` is invoked; for `void` results, `fn()` is invoked.

### `just(value)` / `just_error(error)` / `just_stopped()`

- `just(value)` → immediately ready awaitable producing `value`.
- `just_error(error)` → throws on `await_resume()`:
  - If `std::error_code` → throws `std::system_error`.
  - If `std::exception_ptr` → rethrows the exception.
  - Otherwise → throws `error` directly.
- `just_stopped()` → completes by invoking `unhandled_stopped` on the awaiting promise (if supported).

### `split(awaitable)`

Converts an awaitable to a `shared_task` so it can be awaited multiple times.

---

## `coio::async_scope`
Header: `#include <coio/core.h>`

Scope object for fire-and-forget tasks.

- `spawn(awaitable)` → runs `awaitable` in the background.
- `join()` → awaitable that completes when all spawned tasks finish.

---

## Timers
Header: `#include <coio/utils/timer.h>`

### `coio::timer<Scheduler>`

- `wait(duration)` / `wait_until(time_point)` → **blocking** wait using `std::this_thread::sleep_*`.
- `async_wait(duration)` / `async_wait_until(time_point)` → non-blocking wait via scheduler.

---

## Async I/O Utilities
Header: `#include <coio/asyncio/io.h>`

### I/O Concepts

- `input_device<T>`: has `read_some(std::span<std::byte>)` returning integral.
- `output_device<T>`: has `write_some(std::span<const std::byte>)` returning integral.
- `async_input_device<T, StopToken>`: has `async_read_some(...)` returning awaitable integral.
- `async_output_device<T, StopToken>`: has `async_write_some(...)` returning awaitable integral.
- `dynamic_buffer<T>`: supports `prepare`, `commit`, `consume`, and `data()` access.

### Read/Write helpers

- `read(device, buffer)` → blocks until `buffer` is fully read.
- `write(device, buffer)` → blocks until `buffer` is fully written.
- `async_read(device, buffer, stop_token)` → awaits until `buffer` is fully read.
- `async_write(device, buffer, stop_token)` → awaits until `buffer` is fully written.

### `read_until` / `async_read_until`

Read into a dynamic buffer until a delimiter is found.

- Delimiter can be a `char` or `std::string_view`.
- Returns the number of bytes consumed up to and including the delimiter; returns `0` on EOF.

### Byte view helpers

- `as_bytes(...)` → `std::span<const std::byte>`.
- `as_writable_bytes(...)` → `std::span<std::byte>`.

---

## Networking (Linux only)

> **Note**: Networking backends are not implemented on Windows yet.

### Address types
Header: `#include <coio/net/basic.h>`

- `ipv4_address`: construct from string or 4 bytes.
- `ipv6_address`: construct from string.
- `ip_address`: tagged union of v4/v6.
- `endpoint`: `ip_address` + port.

### Protocol descriptors

- `tcp` (`#include <coio/net/tcp.h>`) → `tcp::v4()` / `tcp::v6()`
- `udp` (`#include <coio/net/udp.h>`) → `udp::v4()` / `udp::v6()`

### `basic_socket<Protocol, IoScheduler>`
Header: `#include <coio/net/socket.h>`

**Core operations**

- `open(protocol)` / `close()` / `release()`
- `is_open()`, `native_handle()`
- `bind(endpoint)`
- `connect(endpoint)` / `async_connect(endpoint, stop_token)`
- `local_endpoint()` / `remote_endpoint()`
- `set_option(opt)` / `get_option(opt)`
- `shutdown(shutdown_send|shutdown_receive|shutdown_both)`
- `cancel()` (cancel pending async ops)

**Socket options** (examples)

- `reuse_address`, `keep_alive`, `broadcast`, `linger`, `send_buffer_size`, `receive_buffer_size`, `v6_only`, `no_delay`.

### `basic_socket_acceptor<Protocol, IoScheduler>`
Header: `#include <coio/net/socket.h>`

- `listen(backlog)`
- `accept()` / `accept(scheduler)`
- `async_accept(stop_token)` / `async_accept(scheduler, stop_token)`

**Concurrency notes**

- Do **not** call multiple `accept`/`async_accept` concurrently on the same acceptor.
- Concurrent initiating functions (those starting with `async_`) on the same socket are undefined.

### `basic_stream_socket<Protocol, IoScheduler>`
Header: `#include <coio/net/socket.h>`

- `read_some(buffer)` / `write_some(buffer)`
- `receive(buffer)` / `send(buffer)` (aliases)
- `async_read_some(buffer, stop_token)` / `async_write_some(buffer, stop_token)`
- `async_receive(...)` / `async_send(...)` (aliases)

**EOF behavior**

- `read_some`/`async_read_some` throw `coio::error::eof` if 0 bytes are read while a non-empty buffer was requested.

**Concurrency notes**

- Do **not** start another read while a read is outstanding on the same socket.
- Do **not** start another write while a write is outstanding on the same socket.

### `basic_datagram_socket<Protocol, IoScheduler>`
Header: `#include <coio/net/socket.h>`

- `receive(buffer)` / `send(buffer)`
- `receive_from(buffer, peer)` / `send_to(buffer, peer)`
- `async_receive(...)`, `async_send(...)`
- `async_receive_from(...)`, `async_send_to(...)`

**Concurrency notes**

- Do **not** issue multiple concurrent receives or multiple concurrent sends on the same socket.

### Resolver
Header: `#include <coio/net/resolver.h>`

- `resolve(query)` / `resolve(protocol, query)` → returns `generator<result_t>`
- `async_resolve(...)` → returns `task<generator<result_t>>`

`resolve_query_t` fields:

- `host_name`, `service_name`, `flags` (e.g. `canonical_name`, `v4_mapped`, `address_configured`)

---

## Synchronization Primitives
Header: `#include <coio/sync_primitives.h>`

### `async_mutex`

A mutex for coroutines that suspends waiters instead of blocking threads.

**API**

| Method | Return | Description |
|--------|--------|-------------|
| `lock()` | awaitable | Acquires the lock, suspending if held by another |
| `try_lock()` | `bool` | Non-blocking attempt to acquire |
| `unlock()` | `void` | Releases the lock, resuming a waiter if any |
| `lock_guard()` | awaitable → `async_unique_lock` | RAII lock acquisition |

**Example**

```cpp
coio::async_mutex mutex;

auto critical_section() -> coio::task<> {
    auto lock = co_await mutex.lock_guard();
    // ... protected code ...
}  // lock released automatically
```

### `async_unique_lock<AsyncMutex>`

RAII lock wrapper for `async_mutex`.

**API**

| Method | Return | Description |
|--------|--------|-------------|
| `lock()` | awaitable | Acquires the lock |
| `try_lock()` | `bool` | Non-blocking attempt |
| `unlock()` | `void` | Releases the lock |
| `owns_lock()` | `bool` | Whether lock is currently held |
| `mutex()` | `mutex_type*` | Pointer to the managed mutex |
| `release()` | `mutex_type*` | Releases ownership without unlocking |

**Construction**

```cpp
async_unique_lock(mutex, std::adopt_lock);   // Already locked
async_unique_lock(mutex, std::defer_lock);   // Don't lock yet
async_unique_lock(mutex, std::try_to_lock);  // Try to lock
```

### `async_semaphore<LeastMaxValue>`

A counting semaphore for coroutines.

**Template Parameters**

- `LeastMaxValue`: Maximum count (default: `std::numeric_limits<...>::max()`)

**API**

| Method | Return | Description |
|--------|--------|-------------|
| `acquire()` | awaitable | Decrements count, suspends if zero |
| `try_acquire()` | `bool` | Non-blocking decrement attempt |
| `release()` | awaitable | Increments count, resumes a waiter if any |
| `count()` | `count_type` | Current count |
| `max()` | `count_type` | Maximum count (static) |

**Example**

```cpp
coio::async_semaphore<> sem{3};  // Initial count = 3

co_await sem.acquire();  // count -> 2
co_await sem.acquire();  // count -> 1
sem.release();           // count -> 2
```

**Type Alias**

- `async_binary_semaphore` = `async_semaphore<1>`

### `async_latch`

A single-use barrier for coordinating coroutines.

**API**

| Method | Return | Description |
|--------|--------|-------------|
| `count_down(n=1)` | `count_type` | Decrements count, returns new value |
| `wait()` | awaitable | Suspends until count reaches zero |
| `arrive_and_wait(n=1)` | awaitable | `count_down(n)` then `wait()` |
| `try_wait()` | `bool` | Returns true if count is zero |
| `count()` | `count_type` | Current count |
| `max()` | `count_type` | Maximum count (static) |

**Example**

```cpp
coio::async_latch latch{3};

// Workers decrement the latch
auto worker = [&]() -> coio::task<> {
    // ... do work ...
    latch.count_down();
};

// Coordinator waits for all workers
auto coordinator = [&]() -> coio::task<> {
    co_await latch.wait();
    // All workers have arrived
};
```

---

## Containers & Utilities

### `conqueue<T, Alloc, Container>`
Header: `#include <coio/utils/conqueue.h>`

**Multi-producer / multi-consumer** async queue.

**API**

| Method | Return | Description |
|--------|--------|-------------|
| `push(value)` | awaitable | Enqueue a value |
| `emplace(args...)` | awaitable | Construct and enqueue in-place |
| `pop()` | awaitable → `T` | Dequeue a value, suspends if empty |
| `try_pop()` | awaitable → `optional<T>` | Non-blocking dequeue attempt |
| `pop_all()` | awaitable → `Container` | Dequeue all values |

**Example**

```cpp
coio::conqueue<std::string> queue;

// Producer
co_await queue.emplace("hello");
co_await queue.push(std::string{"world"});

// Consumer
auto value = co_await queue.pop();  // "hello" or "world"
```

### `ring_buffer<T, Container>` / `inplace_ring_buffer<T, N>`
Header: `#include <coio/utils/conqueue.h>`

**Single-producer / single-consumer** async ring buffer with fixed capacity.

### `inplace_vector<T, N>`
Header: `#include <coio/utils/inplace_vector.h>`

Fixed-capacity vector with inline storage (no heap allocation). Similar to `std::inplace_vector` (C++26).

### `basic_fixed_string<CharType, N>` / `fixed_string<N>`
Header: `#include <coio/utils/fixed_string.h>`

Fixed-size compile-time string with STL-like interface. Useful as non-type template parameters.

### `signal_set`
Header: `#include <coio/utils/signal_set.h>`

Async signal handling.

**API**

| Method | Return | Description |
|--------|--------|-------------|
| `add(signum)` | `void` | Register a signal to listen for |
| `remove(signum)` | `void` | Unregister a signal |
| `clear()` | `void` | Unregister all signals |
| `async_wait()` | awaitable → `int` | Wait for a signal, returns signal number |

**Example**

```cpp
coio::signal_set signals{SIGINT, SIGTERM};

auto watchdog = [&](io_context& ctx) -> coio::task<> {
    const int signum = co_await signals.async_wait();
    std::cout << "Received signal: " << signum << '\n';
    ctx.request_stop();
};
```

---

## Error Handling

### `coio::error::misc_errc`

Library-specific error codes.

| Code | Description |
|------|-------------|
| `eof` | End of file/stream reached |

**Usage**

```cpp
try {
    co_await socket.async_read_some(buffer, stop_token);
}
catch (const std::system_error& e) {
    if (e.code() == coio::error::misc_errc::eof) {
        // Connection closed by peer
    }
}
```

---

## Thread Safety

### General Rules

1. **Execution contexts** (`run_loop`, `epoll_context`, `uring_context`) are **not** thread-safe. Call `run()` from a single thread only.

2. **Synchronization primitives** (`async_mutex`, `async_semaphore`, `async_latch`) are designed for **coroutine-level** synchronization, not thread-level. They are safe to use from coroutines running on different threads, but the underlying execution context must be properly synchronized.

3. **Socket operations**: Do not issue concurrent operations of the same type on a single socket:
   - Do **not** call multiple `async_read_some` concurrently.
   - Do **not** call multiple `async_write_some` concurrently.
   - One read and one write concurrently is **allowed**.

4. **conqueue**: Thread-safe for multi-producer/multi-consumer use.

### Stop Tokens

Most async operations accept a `StopToken` parameter for cancellation:

```cpp
coio::inplace_stop_source stop_source;
auto stop_token = stop_source.get_token();

// In another coroutine or thread:
stop_source.request_stop();

// The async operation will complete with stopped state
co_await socket.async_read_some(buffer, stop_token);
```