# Thread Safety

This page consolidates coio's thread-safety contracts in one place. Each section states the contract; the linked pages carry the full API detail. Two distinct notions are used throughout — keep them apart:

- **Concurrent calls** (thread-safety proper): two threads invoking member functions on the same object at the same time.
- **Outstanding operations**: async operations initiated and not yet complete. Limits on these are independent of thread-safety.

## At a glance

| Component | Concurrent use | Where completions run |
|-----------|----------------|-----------------------|
| Execution contexts | MPSC: initiate from any thread; one consumer | context consumer thread — all three channels |
| Timer queues (`schedule_at`/`schedule_after`) | any thread may submit | context consumer thread |
| `coio::timer` | `async_wait`/`cancel` from any thread | context consumer thread |
| Sockets / acceptors / files / pipes | **not** thread-safe; serialize calls | context consumer thread |
| Sync primitives (`async_mutex`, `async_semaphore`, `async_latch`) | fully thread-safe | releaser's thread (or inline at start) |
| `fifo<T>` | fully thread-safe (MPMC) | peer's thread (or inline at start) |
| `async_scope` | `spawn`/`request_stop`/`close`/`join` from any thread | wherever the spawned sender completes |
| `signal_wait` | start/cancel from any thread | receiver's start-scheduler (else an unspecified thread) |
| `task`, `generator` | single consumer at a time | n/a |
| Buffers (`flat_buffer`, `streambuf`) and other plain types | not thread-safe (like standard containers) | n/a |

## Execution contexts: MPSC

All execution contexts (`time_loop`, `epoll_context`, `uring_context`, `iocp_context`) are **multi-producer, single-consumer** ([details](execution/contexts.md)):

- **Multi-producer**: any thread may concurrently start operations on a context (`schedule()`, `schedule_at()`/`schedule_after()`, the `async_*` operations of its I/O objects), and `get_scheduler()`, `work_started()`/`work_finished()`, and `request_stop()` are thread-safe.
- **Single-consumer**: at most one thread at a time may be inside `run()`, `run_one()`, `poll()`, or `poll_one()`. This precondition is not checked at runtime — concurrent consumer calls are undefined behavior. The consumer thread may change over the context's lifetime, provided the earlier call happens-before the later one (thread join, mutex, or similar).

**Completion-channel guarantee**: work submitted to a context is completed by its active `run()`/`poll()` consumer thread, on **all three channels** — `set_value`, `set_error`, and `set_stopped`, including synchronous initiation failures and cancellation. Every completion goes through the context's operation queue; the initiating thread is never called back inline. Context senders advertise this via `get_completion_scheduler` for all three CPOs, letting the library's scheduler-affinity machinery skip a redundant re-schedule when execution is already on the right scheduler.

The contexts' internal timer queues follow the same contract: timed operations may be submitted from any thread (internally a lock-protected intrusive timer heap; the lock-free MPSC structure is the separate completion op-queue) and fire on the consumer thread. [`coio::timer`](utils/timer.md) inherits this, and its `cancel()` is thread-safe.

## Sockets, acceptors, and other I/O objects

Like Asio, coio socket/acceptor (and file/pipe) objects are **not thread-safe**: member functions must not be called concurrently on the same object without external synchronization ([details](net/sockets.md)). Operations may be *initiated* from threads other than the context consumer, but all initiating calls for a given object must be serialized (a mutex, or funneling through one owning task). If all operations on an object are initiated from work running on the owning context's consumer thread, that thread is an implicit strand.

Independently, per-object **outstanding-operation limits** apply (Asio-style):

- Stream sockets: at most one outstanding read and one outstanding write (one of each may overlap; two reads — malformed).
- Acceptors: at most one outstanding `accept`/`async_accept`.

**Lifetime**: an I/O object must outlive its operations, and a sender obtained from it must be connected and started *before* the object is closed or destroyed; a stale start is undefined behavior (on `epoll_context` it can silently corrupt an unrelated object's bookkeeping rather than fail with `EBADF`).

## Synchronization primitives

[`async_mutex`, `async_semaphore`, `async_latch`](utils/synchronization.md) are safe to use concurrently from any threads — that is their purpose.

!!! note "Resumption thread"
    At the sender level these primitives complete queued waiters on the releasing thread — the thread calling `unlock()`, `release()`, or the `count_down()` that reaches zero — and an operation that can complete immediately (uncontended lock, available permit, counter already zero) completes synchronously on the initiating thread; no completion scheduler is advertised. Inside a `coio::task` with an associated scheduler this is invisible: awaited senders are scheduler-affine, so execution automatically resumes on the task's scheduler after the `co_await`. Only without an associated scheduler does the continuation run inline on the releasing thread.

[`fifo<T>`](utils/buffers.md#fifo) is a fully thread-safe MPMC channel with the same resumption behavior: at the sender level a waiting consumer completes on the producer's thread and vice versa. Its destructor blocks until outstanding async operations finish.

## async_scope

[`async_scope`](utils/async-scope.md): `spawn`, `spawn_on`, `spawn_future`, `spawn_future_on`, `request_stop`, `close`, and `get_token` may be called concurrently from multiple threads, concurrently with spawned work completing; the `join()` sender may be started while spawns are still occurring. The scope must be joined before destruction (unless never used). `async_scope` does not schedule anything: spawned work runs and completes wherever its sender does.

## signal_wait

[`signal_wait`](utils/signal-wait.md) senders may be started and cancelled from any thread. `signal_wait` completes on the receiver's start-scheduler; if the receiver has no start-scheduler, it completes on an unspecified thread.

## Coroutine types

- `coio::task` is move-only and single-shot: it is awaited/started by exactly one consumer. A task with an associated scheduler is scheduler-affine: awaited senders resume the body on that scheduler. Without one (e.g. `inline_task`), the body runs wherever the awaited sender completes — which may be a releaser's thread after awaiting a sync primitive, or an unspecified thread after `signal_wait`.
- `coio::generator` is a synchronous, single-pass range; do not iterate one generator from multiple threads.

## Everything else

Plain value types — `flat_buffer`, `streambuf`, `inplace_vector`, `zstring_view`, `fixed_string`, `scope_exit`, `async_result`, and the like — have standard-container semantics: concurrent `const` access is fine, any mutation requires external synchronization. `retain_ptr`'s reference count (via `retain_base`) is atomic, so distinct `retain_ptr` instances pointing at one object may be copied/destroyed concurrently; a single `retain_ptr` instance is not thread-safe.

## See also

- [Execution contexts](execution/contexts.md) — the MPSC model in full
- [Sockets](net/sockets.md) — concurrency rules and outstanding-op limits
- [Synchronization primitives](utils/synchronization.md)
- [async_scope](utils/async-scope.md) · [signal_wait](utils/signal-wait.md) · [timer](utils/timer.md)
- [Error handling](error-handling.md) — completion channels
