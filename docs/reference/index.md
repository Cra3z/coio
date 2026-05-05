# Reference

This section documents the complete public API of **coio**.

## Conventions

### Header Map

| Area | Header |
|------|--------|
| Core concepts + algorithms | `<coio/core.h>` |
| task | `<coio/task.h>` |
| generator | `<coio/generator.h>` |
| time_loop + work_guard | `<coio/execution_context.h>` |
| epoll backend (Linux) | `<coio/asyncio/epoll_context.h>` |
| io_uring backend (Linux) | `<coio/asyncio/uring_context.h>` |
| IOCP backend (Windows) | `<coio/asyncio/iocp_context.h>` |
| I/O algorithms | `<coio/asyncio/io.h>` |
| File I/O | `<coio/asyncio/file.h>` |
| Pipe | `<coio/asyncio/pipe.h>` |
| Networking basics | `<coio/net/basic.h>` |
| TCP | `<coio/net/tcp.h>` |
| UDP | `<coio/net/udp.h>` |
| Sockets | `<coio/net/socket.h>` |
| Resolver | `<coio/net/resolver.h>` |
| timer | `<coio/utils/timer.h>` |
| async_scope | `<coio/utils/async_scope.h>` |
| Sync primitives | `<coio/sync_primitives.h>` |
| Stop tokens | `<coio/utils/stop_token.h>` |
| signal_set | `<coio/utils/signal_set.h>` |
| conqueue | `<coio/utils/conqueue.h>` |
| flat_buffer | `<coio/utils/flat_buffer.h>` |
| streambuf | `<coio/utils/streambuf.h>` |

### Senders, Awaitables, and Composition

- Many coio operations are **senders** (P2300). They can be composed with `std::execution` algorithms.
- `coio::task<T, Alloc>` is both a coroutine type and a sender.
- Inside a `coio::task`, you can `co_await` a sender — coio wires sender-to-awaitable via `await_transform`.

### Stop Tokens and Cancellation

- Many async operations support cooperative cancellation via [stop tokens](https://en.cppreference.com/w/cpp/thread/stop_token).
- Cancellation follows the sender/receiver contract: cancellation completes with `set_stopped()`.

### Namespace

All public API lives in `namespace coio`. Sub-namespaces:

- `coio::execution` — re-exports of `std::execution` CPOs
- `coio::error` — error codes and categories
- `coio::this_thread` — synchronous waiting functions

### Thread Safety Indicators

Throughout this reference, member functions are documented with thread safety guarantees:

- **Thread-safe** — may be called concurrently from multiple threads on the same object
- **Not thread-safe** — must not be called concurrently without external synchronization
