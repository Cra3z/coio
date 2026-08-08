# Buffers & Channels

Three data-holding utilities used with (but not tied to) coio's I/O layer: `flat_buffer`, a contiguous dynamic byte buffer with a prepare/commit/consume protocol; `streambuf`, the same protocol layered over `std::streambuf` for iostream interop; and `fifo<T>`, a thread-safe async MPMC queue ("channel") for passing values between tasks.

Headers: `#include <coio/utils/flat_buffer.h>`, `#include <coio/utils/streambuf.h>`, `#include <coio/utils/fifo.h>`

## Overview

| Type | Element | Protocol | Thread-safe |
|------|---------|----------|-------------|
| `flat_buffer` (`basic_flat_buffer<Alloc>`) | `std::byte` | `prepare` / `commit` / `consume` | no |
| `streambuf` (`basic_streambuf<Alloc>`) | `char` (as `std::byte` spans) | `prepare` / `commit` / `consume` + `std::streambuf` | no |
| `fifo<T, Queue>` | `T` | `async_push` / `async_pop` (+ `try_*`) | yes (MPMC) |

The prepare/commit/consume protocol (as in Asio/Beast dynamic buffers): `prepare(n)` returns writable space, `commit(n)` moves freshly written bytes into the readable region, `data()` views readable bytes, `consume(n)` discards them from the front.

## Synopsis

```cpp
namespace coio {
    template<typename Alloc>
    class basic_flat_buffer {
    public:
        using allocator_type = /* Alloc rebound to std::byte */;

        basic_flat_buffer();
        explicit basic_flat_buffer(const allocator_type& alloc) noexcept;
        explicit basic_flat_buffer(std::size_t max_size,
                                   const allocator_type& alloc = {}) noexcept;
        basic_flat_buffer(const basic_flat_buffer&);
        basic_flat_buffer(basic_flat_buffer&&) noexcept;
        // copy/move assignment, destructor, swap

        [[nodiscard]] auto get_allocator() const noexcept -> allocator_type;
        [[nodiscard]] auto size() const noexcept -> std::size_t;      // readable bytes
        [[nodiscard]] auto empty() const noexcept -> bool;
        [[nodiscard]] auto max_size() const noexcept -> std::size_t;
        [[nodiscard]] auto capacity() const noexcept -> std::size_t;
        [[nodiscard]] auto data() noexcept -> std::span<std::byte>;
        [[nodiscard]] auto data() const noexcept -> std::span<const std::byte>;
        [[nodiscard]] auto cdata() const noexcept -> std::span<const std::byte>;

        [[nodiscard]] auto prepare(std::size_t n) -> std::span<std::byte>;
        auto commit(std::size_t n) noexcept -> void;
        auto consume(std::size_t n) noexcept -> void;

        auto clear() noexcept -> void;
        auto reserve(std::size_t n) -> void;
        auto shrink_to_fit() -> void;
    };

    using flat_buffer = basic_flat_buffer<std::allocator<std::byte>>;

    template<typename Allocator>
    class basic_streambuf : public std::streambuf {
    public:
        explicit basic_streambuf(
            std::size_t max_size = std::numeric_limits<std::size_t>::max(),
            const Allocator& allocator = Allocator());
        // non-copyable

        [[nodiscard]] auto size() const noexcept -> std::size_t;
        [[nodiscard]] auto max_size() const noexcept -> std::size_t;
        [[nodiscard]] auto capacity() const noexcept -> std::size_t;
        [[nodiscard]] auto data() const noexcept -> std::span<const std::byte>;
        [[nodiscard]] auto prepare(std::size_t n) -> std::span<std::byte>;
        auto commit(std::size_t n) -> void;
        auto consume(std::size_t n) -> void;
    };

    using streambuf = basic_streambuf<std::allocator<char>>;

    template<typename T, typename Queue = std::queue<T>>
    class fifo {
    public:
        using container_type = Queue;
        using value_type = T;
        using size_type = typename Queue::size_type;

        fifo();                                       // default-constructs the queue
        template<typename... Args>
        explicit fifo(std::in_place_t, Args&&... args); // constructs the queue in place
        fifo(const fifo&) = delete;
        ~fifo();                                      // closes, then blocks until pending ops finish

        [[nodiscard]] auto empty() const noexcept -> bool;
        [[nodiscard]] auto size() const noexcept -> size_type;
        [[nodiscard]] auto max_size() const noexcept -> size_type;

        [[nodiscard]] auto async_push(value_type value);       // sender: set_value() | set_stopped()
        template<typename... Args>
        [[nodiscard]] auto async_emplace(Args... args);        // sender: set_value() | set_stopped()
        [[nodiscard]] auto async_pop() noexcept;               // sender: set_value(T) | set_stopped()

        [[nodiscard]] auto try_push(value_type value) -> bool;
        template<typename... Args>
        [[nodiscard]] auto try_emplace(Args... args) -> bool;
        [[nodiscard]] auto try_pop() noexcept -> std::optional<value_type>;

        auto close() noexcept -> void;
    };
}
```

## API Reference

### flat_buffer

`basic_flat_buffer<Alloc>` is a dynamic buffer with a single contiguous allocation, modeled on `boost::beast::flat_buffer`:

```
|<-- consumed -->|<-- readable data -->|<-- prepared -->|<-- free -->|
```

- `prepare(n) -> span<byte>` — returns `n` writable bytes past the readable region, compacting (moving readable bytes to the front) or reallocating as needed. **Throws** `std::length_error` if `size() + n` would exceed `max_size()`. Invalidates previous `data()`/`prepare()` spans when it compacts or reallocates.
- `commit(n)` — appends up to `n` bytes of the most recently prepared region to the readable region.
- `data()` — span over the readable region; `consume(n)` removes up to `n` bytes from its front (cheap: just moves the read offset).
- `max_size` caps `size()`; set it via the `basic_flat_buffer(max_size, alloc)` constructor. `reserve(n)` / `shrink_to_fit()` manage capacity; `clear()` empties the buffer without deallocating.
- Copyable (copies only the readable bytes) and movable; allocator-aware (`get_allocator`, POCCA/POCMA/POCS honored).

### streambuf

`basic_streambuf<Allocator>` derives from `std::streambuf` (adapted from `asio::streambuf`): the same `prepare`/`commit`/`consume`/`data` protocol as `flat_buffer`, plus everything a `std::streambuf` supports — so you can wrap it in `std::istream`/`std::ostream` to parse or format data that is read from / written to async I/O.

- `prepare(n)` grows the internal storage as needed and **throws** `std::length_error` when `size() + n` would exceed `max_size` (also enforced when iostream output grows the buffer via `overflow`).
- `size()` is the number of readable bytes (`data()` spans them); `commit(n)` moves prepared bytes into the readable region; `consume(n)` discards from the front.
- Non-copyable, non-movable.

### fifo

`fifo<T, Queue>` is an asynchronous multi-producer multi-consumer queue built from two `async_semaphore`s (slots and items) plus a small internal scope. `T` must be a cv-unqualified, nothrow-move-constructible object type; `Queue` any `std::queue`-like adaptor with `value_type` `T`.

**Capacity** — the fifo is bounded by `max_size()`, which is `Queue::max_size()` if the queue type provides it, otherwise unbounded (`numeric_limits<size_type>::max()`). With the default `std::queue`, the fifo is effectively unbounded and `async_push` never waits for space; use a queue type exposing `max_size()` to get a bounded channel with backpressure.

- `async_push(value)` / `async_emplace(args...)` — sender: waits (suspends) until a slot is free, then enqueues; completes with `set_value()`. Completes with `set_stopped()` if the fifo is closed or the operation's stop token fires while waiting. If constructing/enqueuing the element can throw, the sender additionally completes with `set_error(std::exception_ptr)`. Arguments are taken by value (they must be move-constructible) because the operation may run after the call returns.
- `async_pop()` — sender: waits until an element is available, then dequeues and completes with `set_value(T)`; `set_stopped()` on close/cancellation.
- `try_push` / `try_emplace` / `try_pop` — synchronous, never wait; `try_pop` returns `std::nullopt` when empty, `try_push` returns `false` when full. They keep working after `close()`.
- `close()` — requests stop on the internal scope: pending `async_push`/`async_pop` operations complete with `set_stopped()`, and async operations started afterwards complete with `set_stopped()` without waiting. Idempotent.
- `~fifo()` — calls `close()` and then **blocks** (`sync_wait`) until all outstanding async operations have finished.
- `size()` / `empty()` — snapshots; may be stale immediately in concurrent use.

!!! note "Where do fifo continuations run?"
    Like the [synchronization primitives](synchronization.md) it is built on, `fifo` completes a waiting consumer's operation on the producer's thread (and a waiting producer's on the consumer's thread); an operation that completes without waiting completes synchronously on the initiating thread. Inside a `coio::task` with an associated scheduler this is invisible: awaited senders are scheduler-affine, so execution automatically resumes on the task's scheduler after the `co_await`. Only without an associated scheduler does the continuation run inline on the peer's thread.

## Example

Two writer tasks and four reader tasks on six event-loop threads sharing one channel (from `examples/fifo.cpp`, abridged):

```cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/fifo.h>

coio::fifo<std::string> channel;

auto writer(std::initializer_list<std::string_view> datum) -> coio::task<> {
    for (auto str : datum) {
        co_await channel.async_emplace(str);
    }
}

auto reader(std::string_view name) -> coio::task<> {
    while (true) {
        auto str = co_await channel.async_pop();
        std::println("{} reads {}", name, str);
        if (str == "bye") break;
    }
}

// started with coio::starts_on(worker.scheduler(), writer(...)) etc.,
// then joined with coio::when_all + sync_wait — see examples/fifo.cpp.
```

## See also

- [I/O model](../io/model.md) — how buffers are used by read/write operations
- [I/O algorithms](../io/algorithms.md) — `async_read_until` with dynamic buffers
- [Synchronization primitives](synchronization.md) — `async_semaphore` underlying `fifo`
- [Thread safety](../thread-safety.md)
