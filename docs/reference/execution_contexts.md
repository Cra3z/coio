# Execution Contexts

**Header:** `<coio/execution_context.h>`

This page documents `time_loop`, `work_guard`, and the `execution_context` concept — the shared infrastructure for all coio event loops.

---

## `coio::time_loop`

An execution context with a timer queue and a manually-driven event loop. It is the simplest context and serves as the base for platform-specific I/O contexts.

### Member Types

| Type | Description |
|------|-------------|
| `scheduler` | The associated scheduler type. Models `execution::scheduler` and `timed_scheduler`. |

### Member Functions

#### Lifecycle

| Name | Description |
|------|-------------|
| `time_loop()` | Default constructor. Uses `std::pmr::get_default_resource()`. |
| `explicit time_loop(std::pmr::memory_resource&) noexcept` | Constructs with a custom memory resource. |
| `~time_loop()` | Destructor. |

Copy construction and copy assignment are **deleted**.

#### Context Operations

| Name | Description |
|------|-------------|
| `get_scheduler() noexcept` | Returns a `scheduler` associated with this context. Thread-safe. |
| `get_allocator() const noexcept` | Returns a `std::pmr::polymorphic_allocator<>`. |
| `request_stop()` | Signals the context to stop processing. Thread-safe. |
| `work_started() noexcept` | Increments the internal work count, keeping `run()` from returning. |
| `work_finished() noexcept` | Decrements the internal work count. |

#### Event Loop

| Name | Description |
|------|-------------|
| `poll_one() -> bool` | Processes at most one ready work item without blocking. Returns `true` if an item was processed. |
| `poll() -> std::size_t` | Processes all ready work items without blocking. Returns the count processed. |
| `run_one() -> bool` | Blocks until one work item becomes ready and processes it. Returns `true` if an item was processed. |
| `run() -> std::size_t` | Processes work items until the work count reaches zero or stop is requested. Blocks when idle. Returns the total count processed. |

All `poll_*` and `run_*` methods **may be called concurrently from multiple threads**.

### `time_loop::scheduler`

| Name | Description |
|------|-------------|
| `schedule() const noexcept` | Returns a sender that completes when executed on this context. |
| `schedule_after(duration) const noexcept` | Returns a sender that completes after the given duration, on this context. |
| `schedule_at(time_point) const noexcept` | Returns a sender that completes at the given time point, on this context. |
| `static now() noexcept` | Returns `std::chrono::steady_clock::now()`. |
| `context() const noexcept` | Returns a reference to the owning `time_loop`. |
| `static query(get_forward_progress_guarantee_t) noexcept` | Returns `forward_progress_guarantee::parallel`. |
| `operator==(const scheduler&, const scheduler&) = default` | Schedule from the same context compares equal. |

### Example

```cpp
#include <coio/execution_context.h>
#include <coio/core.h>
#include <iostream>

int main() {
    coio::time_loop context;
    auto sched = context.get_scheduler();

    // Schedule a delayed task
    coio::async_scope scope;
    scope.spawn(
        sched.schedule_after(std::chrono::seconds{1})
        | coio::then([] { std::cout << "Hello after 1s\n"; })
    );

    context.run(); // process work until idle
    coio::this_thread::sync_wait(scope.join());
}
```

### Multi-threading

Multiple threads can call `run()` concurrently. Work submitted to the context may be executed by **any** thread currently calling `run()` or `poll()`:

```cpp
coio::time_loop context;
std::vector<std::jthread> workers;
for (int i = 0; i < 4; ++i) {
    workers.emplace_back([&context] { context.run(); });
}
// Submit work from main thread...
```

---

## `coio::work_guard`

**Header:** `<coio/execution_context.h>`

An RAII guard that increments the context work count on construction and decrements on destruction. Keeps `run()` from returning while work is outstanding.

### Template Parameters

| Parameter | Description |
|-----------|-------------|
| `ExecutionContext` | Must model `execution_context`. |

### Member Functions

| Name | Description |
|------|-------------|
| `work_guard()` | Default constructor. Creates an unattached guard. |
| `explicit work_guard(ExecutionContext& context) noexcept` | Attaches to a context and increments work count. |
| `work_guard(const work_guard& other) noexcept` | Copy constructor. Increments work count on the same context. |
| `work_guard(work_guard&& other) noexcept` | Move constructor. Transfers ownership, does not change work count. |
| `~work_guard()` | Destructor. Decrements work count. |
| `operator=(work_guard other) noexcept` | Move/copy assignment via copy-and-swap. |

### Example

```cpp
coio::time_loop context;
{
    coio::work_guard guard{context}; // work_started
    // spawn work...
} // work_finished on scope exit
context.run(); // will return when all work is done
```

---

## `coio::execution_context` (Concept)

```cpp
template<typename T>
concept execution_context = requires(T& context) {
    { context.get_scheduler() } -> execution::scheduler;
    context.work_started();
    context.work_finished();
};
```

All coio execution contexts (`time_loop`, `epoll_context`, `uring_context`, `iocp_context`) satisfy this concept.
