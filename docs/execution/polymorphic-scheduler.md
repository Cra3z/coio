# polymorphic_scheduler

`coio::polymorphic_scheduler` is a type-erased scheduler: a single concrete type that can wrap any scheduler. It is the default scheduler parameter of [`coio::task`](../coroutines/task.md), which is what lets a plain `task<T>` be started from any environment.

Header: `#include <coio/utils/polymorphic_scheduler.h>`

## Overview

Use `polymorphic_scheduler` when a scheduler must be stored or passed without knowing its concrete type — in type-erased interfaces, heterogeneous containers, or (implicitly) whenever you write `coio::task<T>` with the default scheduler parameter. It models `execution::scheduler`: `schedule()` returns a sender that completes with `set_value()` on the wrapped scheduler's execution resource.

The erasure has costs — reference-counted backend allocation at construction, virtual dispatch and possibly a heap-allocated operation state per `schedule()`. When the concrete scheduler type is statically known, prefer it directly (e.g. `epoll_context::task<T>` over `coio::task<T>` in context-bound code), or use `inline_task` when no affinity is needed.

## Synopsis

```cpp
namespace coio {
    class polymorphic_scheduler {
    public:
        using scheduler_concept = execution::scheduler_tag;

        template<typename Sched, typename Alloc = std::allocator<void>>
            // requires: Sched is a scheduler (other than polymorphic_scheduler itself)
            //           whose schedule() cannot complete with an error,
            //           Alloc is an allocator
        explicit polymorphic_scheduler(Sched sched, Alloc alloc = Alloc());

        template<typename Alloc>
        explicit polymorphic_scheduler(polymorphic_scheduler sched, const Alloc&) noexcept;

        [[nodiscard]] auto schedule() const noexcept -> /*sender of ()*/;

        auto query(execution::get_forward_progress_guarantee_t) const noexcept
            -> execution::forward_progress_guarantee;

        friend auto operator== (const polymorphic_scheduler& lhs,
                                const polymorphic_scheduler& rhs) noexcept -> bool;

        template<typename Sched>   // any other scheduler type
        auto operator== (const Sched& sched) const noexcept -> bool;

        template<typename Sched>
        [[nodiscard]] auto target() const
            noexcept(std::is_nothrow_copy_constructible_v<Sched>) -> std::optional<Sched>;

        auto swap(polymorphic_scheduler& other) noexcept -> void;
        friend auto swap(polymorphic_scheduler& lhs, polymorphic_scheduler& rhs) noexcept -> void;
    };
}

template<typename Alloc>
struct std::uses_allocator<coio::polymorphic_scheduler, Alloc> : std::true_type {};
```

## API Reference

### Construction

```cpp
template<typename Sched, typename Alloc = std::allocator<void>>
explicit polymorphic_scheduler(Sched sched, Alloc alloc = Alloc());
```

Wraps a copy of `sched` in a reference-counted backend allocated with `alloc`. Copying a `polymorphic_scheduler` afterwards only bumps the reference count — no further allocation.

**Constraints:** `Sched` models `execution::scheduler` and is *infallible* — its `schedule()` sender completes only with `set_value()` (plus possibly `set_stopped()` under a stoppable environment). All coio context schedulers and `execution::inline_scheduler` qualify.

**Throws:** whatever allocation with `alloc` throws.

```cpp
template<typename Alloc>
explicit polymorphic_scheduler(polymorphic_scheduler sched, const Alloc&) noexcept;
```

Uses-allocator pass-through: wrapping a `polymorphic_scheduler` in a `polymorphic_scheduler` adds no second layer of erasure; the allocator is ignored. Together with the `std::uses_allocator` specialization, this makes the type well-behaved under `std::make_obj_using_allocator` — which is how `task` constructs its scheduler from the parent environment.

### `schedule`

```cpp
[[nodiscard]] auto schedule() const noexcept -> /*sender of ()*/;
```

A sender with completion signatures `set_value_t()` that completes on the wrapped scheduler's execution resource. Its environment answers `get_completion_scheduler<set_value_t>` with the `polymorphic_scheduler` itself. The returned sender is move-only and single-shot.

Connecting the sender type-erases the wrapped scheduler's operation state: small states are stored inline (a small-buffer of a few pointers); larger ones are allocated using the allocator from the wrapped schedule-sender's own environment (`get_allocator`, defaulting to `std::allocator`).

**Precondition:** the scheduler is non-null (not moved-from).

### Equality

```cpp
friend auto operator== (const polymorphic_scheduler&, const polymorphic_scheduler&) noexcept -> bool;
```

Two `polymorphic_scheduler`s are equal when both are empty, or when they wrap schedulers of the same concrete type that compare equal — so two independent erasures of the same `epoll_context`'s scheduler compare equal, and wrappers of different contexts (or different scheduler types) do not.

```cpp
template<typename Sched>
auto operator== (const Sched& sched) const noexcept -> bool;
```

Cross-type comparison against a concrete scheduler: `true` iff this wrapper holds a `Sched` equal to `sched`. This is what lets scheduler-affinity machinery recognize that a type-erased scheduler and the concrete scheduler it wraps denote the same execution resource, eliding a needless re-schedule.

### Queries

```cpp
auto query(execution::get_forward_progress_guarantee_t) const noexcept
    -> execution::forward_progress_guarantee;
```

Forwarded to the wrapped scheduler. Other scheduler-specific queries (e.g. `get_allocator`, timed-scheduler operations, `make_io_object`) are **not** forwarded — the erasure only preserves the core scheduler contract. Recover the concrete scheduler with `target()` if you need more.

### `target`

```cpp
template<typename Sched>
[[nodiscard]] auto target() const
    noexcept(std::is_nothrow_copy_constructible_v<Sched>) -> std::optional<Sched>;
```

Returns a copy of the wrapped scheduler if its concrete type is exactly `Sched`, otherwise `std::nullopt`. Conditionally noexcept: noexcept exactly when copying `Sched` cannot throw.

### Thread safety

Distinct `polymorphic_scheduler` copies referring to the same backend may be used concurrently (the reference count is atomic). A single object is not thread-safe against concurrent mutation (`swap`, assignment).

## Example

```cpp
#include <iostream>
#include <thread>
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/polymorphic_scheduler.h>

// accepts any scheduler without being a template;
// inline_task: resumes wherever the awaited sender completes
auto run_somewhere(coio::polymorphic_scheduler sched) -> coio::inline_task<> {
    co_await coio::schedule(sched);
    std::cout << "running on the wrapped scheduler's thread\n";
}

auto main() -> int {
    coio::time_loop loop;
    coio::work_guard<coio::time_loop> guard{loop};
    std::jthread worker{[&] { loop.run(); }};

    coio::polymorphic_scheduler sched{loop.get_scheduler()};

    // cross-type equality: the wrapper and the wrapped scheduler compare equal
    std::cout << std::boolalpha << (sched == loop.get_scheduler()) << '\n';   // true

    // recover the concrete scheduler
    if (auto concrete = sched.target<coio::time_loop::scheduler>()) {
        std::cout << "wraps this very time_loop: "
                  << (&concrete->context() == &loop) << '\n';                  // true
    }

    coio::async_scope scope;
    scope.spawn(run_somewhere(sched));
    coio::this_thread::sync_wait(scope.join());
    guard = {};   // let run() return; worker joins before loop is destroyed
}
```

Every default `coio::task<T>` also exercises this type implicitly: at start, the parent environment's scheduler is wrapped into a `polymorphic_scheduler` (using the parent's allocator), and the task body stays affine to it.

## See also

- [task](../coroutines/task.md) — where `polymorphic_scheduler` is the default
- [Execution contexts](contexts.md) — the concrete schedulers you will typically wrap
- [Core Concepts](../concepts.md) — environments and scheduler queries
