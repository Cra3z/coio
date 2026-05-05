# coio::task

**Header:** `<coio/task.h>`

A lazily-started, move-only coroutine type that models an `execution::sender`.

## Template Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `T` | `void` | The return type of the coroutine. Must be `void`, a move-constructible object type, or an lvalue reference type. |
| `Alloc` | `void` | The allocator type. Must be `void` or a `simple_allocator`. |

## Member Types

| Type | Definition |
|------|-----------|
| `promise_type` | *implementation-defined* |
| `sender_concept` | `execution::sender_tag` |
| `completion_signatures` | `execution::completion_signatures<set_value_t(T), set_error_t(std::exception_ptr), set_stopped_t()>` |

## Member Functions

### Constructors and Destructor

| Name | Description |
|------|-------------|
| `task()` | Default constructor. Creates an empty (disconnected) task. |
| `task(task&& other) noexcept` | Move constructor. Transfers ownership of the coroutine handle. |
| `~task()` | Destructor. Destroys the underlying coroutine frame if non-null. |

### Assignment

| Name | Description |
|------|-------------|
| `operator=(task&& other) noexcept` | Move assignment. |

### Observers

| Name | Description |
|------|-------------|
| `explicit operator bool() const noexcept` | Returns `true` if this task holds a coroutine handle. |

### Sender Protocol

| Name | Description |
|------|-------------|
| `connect(Receiver receiver) && noexcept` | Connects the task (as a sender) to a receiver, producing an operation state. |
| `static consteval get_completion_signatures() noexcept` | Returns the completion signatures for the sender. |

### Awaitable Protocol

| Name | Description |
|------|-------------|
| `as_awaitable(ReceiverPromise& receiver) && noexcept` | Converts the task into an awaitable for use inside another coroutine. |

### Other

| Name | Description |
|------|-------------|
| `swap(task& other) noexcept` | Swaps two task objects. |
| `friend swap(task& lhs, task& rhs) noexcept` | Free swap. |

## Completion Signatures

The task completes via one of three channels:

| Channel | Signature |
|---------|----------|
| Value | `set_value(T)` (or `set_value()` when `T = void`) |
| Error | `set_error(std::exception_ptr)` |
| Stopped | `set_stopped()` |

## Behavior

- **Lazy start**: The coroutine body does not begin execution until the task is awaited or the sender operation state is started.
- **Move-only**: Tasks cannot be copied.
- **Stop token inheritance**: When awaited, the task inherits the stop token from the awaiting coroutine's environment.
- **Allocator propagation**: If `Alloc` is not `void`, the coroutine frame is allocated via that allocator.

## Example

```cpp
#include <coio/task.h>
#include <coio/core.h>
#include <iostream>

auto compute(int x) -> coio::task<int> {
    co_return x * 2;
}

auto main_coro() -> coio::task<> {
    int result = co_await compute(21);
    std::cout << result << '\n'; // 42
}

int main() {
    coio::this_thread::sync_wait(main_coro());
}
```

### With Custom Allocator

```cpp
template<typename T = std::byte>
struct my_allocator {
    using value_type = T;
    // ... standard allocator interface
};

auto foo() -> coio::task<void, my_allocator<>> {
    co_return;
}

int main() {
    coio::this_thread::sync_wait(foo());
}
```

## Notes

- `coio::task` replaces `std::future` and other eager future types for coroutine-based async programming.
- Use `coio::this_thread::sync_wait(task)` to synchronously wait for a task from non-coroutine code.
