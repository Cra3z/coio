# Guides

These guides explain how to use coio for common patterns.

## Table of Contents

| Guide | Description |
|-------|-------------|
| [Coroutines](coroutines.md) | Using `task`, `generator`, and `elements_of` |
| [Execution Contexts](execution_contexts.md) | Choosing and configuring event loops |
| [Networking](networking.md) | Building TCP/UDP servers and clients |
| [File I/O](file_io.md) | Reading and writing files asynchronously |
| [Synchronization](synchronization.md) | Using `async_mutex`, `async_semaphore`, `async_latch` |
| [Cancellation](cancellation.md) | Stop tokens and cooperative cancellation |
| [Composing Senders](compose_senders.md) | Composing sender algorithms |
| [Allocators](allocators.md) | Custom allocators with tasks |

## Recommended Reading Order

1. Start with [Coroutines](coroutines.md) to understand the basic building blocks
2. Read [Execution Contexts](execution_contexts.md) to learn how the event loop works
3. Pick your domain: [Networking](networking.md) or [File I/O](file_io.md)
4. Learn about [Synchronization](synchronization.md) and [Cancellation](cancellation.md) as your program grows
5. Dive into [Composing Senders](compose_senders.md) for advanced patterns
