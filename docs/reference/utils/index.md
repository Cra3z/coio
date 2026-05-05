# Utilities

This section documents the utility types provided by coio.

## Overview

| Header | Primary Type(s) | Description |
|--------|----------------|-------------|
| `<coio/utils/timer.h>` | `timer` | Async timer bound to a scheduler |
| `<coio/utils/async_scope.h>` | `async_scope` | Scope for spawning background work |
| `<coio/utils/stop_token.h>` | `inplace_stop_source`, `never_stop_token`, `stop_when` | Cancellation tokens |
| `<coio/utils/signal_set.h>` | `signal_set` | Async signal handling |
| `<coio/utils/conqueue.h>` | `conqueue`, `ring_buffer` | Async concurrent queues |
| `<coio/utils/flat_buffer.h>` | `flat_buffer` | Dynamic buffer for I/O |
| `<coio/utils/streambuf.h>` | `streambuf` | `std::streambuf`-based dynamic buffer |
| `<coio/utils/inplace_vector.h>` | `inplace_vector` | Fixed-capacity vector |
| `<coio/utils/fixed_string.h>` | `fixed_string` | Compile-time fixed-size string |
| `<coio/utils/zstring_view.h>` | `zstring_view` | Null-terminated string view |
| `<coio/utils/retain_ptr.h>` | `retain_ptr`, `make_retain` | Intrusive shared pointer |
| `<coio/utils/allocator_resource.h>` | `allocator_resource` | Allocator ↔ memory_resource adaptation |
| `<coio/utils/scope_exit.h>` | `scope_exit` | RAII scope guard |
| `<coio/utils/atomutex.h>` | `atomutex` | Spinlock |
| `<coio/utils/type_traits.h>` | `type_list` | Compile-time type list |
