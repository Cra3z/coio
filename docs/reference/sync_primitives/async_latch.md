# coio::async_latch

**Header:** `<coio/sync_primitives.h>`

A single-use countdown latch. Coroutines wait until the internal counter reaches zero, at which point all waiters are resumed.

## Member Types

| Type | Description |
|------|-------------|
| `count_type` | `std::atomic_unsigned_lock_free::value_type` |
| `wait_sender` | Sender type returned by `wait()` and `arrive_and_wait()`. |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `explicit async_latch(count_type count) noexcept` | Constructs with the given initial count. |
| *(copy deleted)* | Not copyable. |

### Observers

| Name | Description |
|------|-------------|
| `static constexpr max() noexcept -> count_type` | Returns the maximum supported count. |
| `count() const noexcept -> count_type` | Returns the current remaining count. |
| `try_wait() const noexcept -> bool` | Returns `true` if the count has reached zero. |

### Operations

| Name | Description |
|------|-------------|
| `count_down(count_type n = 1) noexcept -> count_type` | Decrements the counter by `n`. If the count reaches zero, all waiters are resumed. Returns the **new** count (before reaching zero). |
| `wait() noexcept -> wait_sender` | Returns a sender that completes when the latch reaches zero. |
| `arrive_and_wait(count_type n = 1) noexcept -> wait_sender` | Atomically decrements the counter by `n` and returns a sender that waits for zero. |

### wait_sender

| Member | Description |
|--------|-------------|
| `sender_concept = execution::sender_tag` | Models sender. |
| `completion_signatures = execution::completion_signatures<set_value_t()>` | Completes with void. |

## Example

```cpp
#include <coio/sync_primitives.h>
#include <coio/core.h>
#include <string>

struct Job {
    std::string name;
    std::string product;
};

auto work(Job& job, coio::async_latch& work_done,
          coio::async_latch& start_clean_up) -> coio::task<> {
    job.product = job.name + " worked";
    work_done.count_down();
    co_await start_clean_up.wait();
    job.product = job.name + " cleaned";
}

int main() {
    Job jobs[]{{"Annika"}, {"Buru"}, {"Chuck"}};
    coio::async_latch work_done{3};
    coio::async_latch start_clean_up{1};

    coio::async_scope scope;
    for (auto& job : jobs) {
        scope.spawn(work(job, work_done, start_clean_up));
    }

    // Wait for all workers to finish their work phase
    coio::this_thread::sync_wait(work_done.wait());

    // Signal cleanup phase
    start_clean_up.count_down();

    coio::this_thread::sync_wait(scope.join());
}
```

## Notes

- The latch is **single-use**: once the count reaches zero, it cannot be reset.
- `count_down` and `arrive_and_wait` are thread-safe.
- Waiters are stored in an intrusive linked list — no heap allocation for waiting.
