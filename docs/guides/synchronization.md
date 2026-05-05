# Synchronization Guide

coio provides async synchronization primitives that **suspend coroutines** instead of blocking threads. They are safe to use across coroutines that run on different threads.

## async_mutex

An async mutex for protecting shared state accessed from multiple coroutines.

### Basic Locking

```cpp
#include <coio/sync_primitives.h>
#include <coio/core.h>

coio::async_mutex mtx;
int counter = 0;

auto worker(int rounds) -> coio::task<> {
    for (int i = 0; i < rounds; ++i) {
        co_await mtx.lock();
        ++counter;
        mtx.unlock();
    }
}
```

### RAII Locking with lock_guard

The preferred pattern uses `lock_guard()`, which returns a sender completing with an `async_unique_lock`:

```cpp
auto worker(int rounds) -> coio::task<> {
    for (int i = 0; i < rounds; ++i) {
        auto lock = co_await mtx.lock_guard();
        ++counter;
        // lock is automatically released on destruction
    }
}
```

### Manual Unique Lock

For deferred or conditional locking:

```cpp
auto conditional_work() -> coio::task<> {
    coio::async_unique_lock lock{mtx, std::defer_lock};

    // ... do some non-critical work ...

    co_await lock.lock(); // wait to acquire
    // critical section ...
    lock.unlock();
}
```

## async_semaphore

A counting semaphore for limiting concurrency.

### Limiting Concurrent Operations

```cpp
coio::async_semaphore<> sem{4}; // max 4 concurrent

auto worker(int id) -> coio::task<> {
    co_await sem.acquire(); // wait for a slot
    std::println("Worker {} starts", id);
    co_await coio::timer{sched}.async_wait(2s); // do work
    std::println("Worker {} done", id);
    co_await sem.release(); // release slot
}

int main() {
    coio::async_scope scope;
    for (int i = 0; i < 20; ++i) {
        scope.spawn(worker(i));
    }
    coio::this_thread::sync_wait(scope.join());
}
```

### Binary Semaphore (Signal)

```cpp
coio::async_binary_semaphore ready{0};

// Consumer
auto consumer() -> coio::task<> {
    co_await ready.acquire();
    std::println("Producer is done");
}

// Producer
auto producer() -> coio::task<> {
    // ... produce data ...
    co_await ready.release(); // signal consumer
}
```

## async_latch

A single-use countdown latch. All coroutines that `wait()` on it are resumed when the counter reaches zero.

### Worker Synchronization Pattern

```cpp
#include <coio/sync_primitives.h>

auto worker(Job& job, coio::async_latch& work_done,
            coio::async_latch& start_clean_up) -> coio::task<> {
    // Phase 1: do work
    job.product = job.name + " worked";
    work_done.count_down();     // signal: "I'm done with phase 1"

    // Phase 2: wait for cleanup signal
    co_await start_clean_up.wait();
    job.product = job.name + " cleaned";
}

int main() {
    Job jobs[]{{"A"}, {"B"}, {"C"}};
    coio::async_latch work_done{3};      // wait for 3 workers
    coio::async_latch start_clean_up{1}; // gate for phase 2

    coio::async_scope scope;
    for (auto& job : jobs) {
        scope.spawn(worker(job, work_done, start_clean_up));
    }

    // Wait for phase 1 to complete
    coio::this_thread::sync_wait(work_done.wait());
    for (auto& job : jobs) {
        std::println("Phase 1: {}", job.product);
    }

    // Signal phase 2
    start_clean_up.count_down();
    coio::this_thread::sync_wait(scope.join());
    for (auto& job : jobs) {
        std::println("Phase 2: {}", job.product);
    }
}
```

### arrive_and_wait

Atomically decrements and waits:

```cpp
coio::async_latch latch{5};

// Each worker calls:
co_await latch.arrive_and_wait();
// When the 5th worker arrives, all 5 proceed
```

## Choosing the Right Primitive

| Need | Use |
|------|-----|
| Mutual exclusion | `async_mutex` |
| Limit concurrency to N | `async_semaphore<N>` |
| Signal between 2 coroutines | `async_binary_semaphore` |
| Wait for N tasks to complete | `async_latch` |
| Wait for all tasks + collect results | `async_scope::join()` |

## Important Notes

- `unlock()` and `try_lock()` are **synchronous** — they must be called by the lock owner
- `lock()` returns a **sender** — you must `co_await` it
- `async_mutex` is not recursive — locking twice from the same coroutine deadlocks
- `async_latch` is single-use — once the count reaches zero, it cannot be reset
