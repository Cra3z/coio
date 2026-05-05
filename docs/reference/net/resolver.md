# coio::basic_resolver

**Header:** `<coio/net/resolver.h>`

Resolves hostnames and service names to endpoint lists.

## Template Parameters

| Parameter | Description |
|-----------|-------------|
| `Protocol` | Protocol descriptor (e.g., `tcp`, `udp`). |
| `Scheduler` | Must model `execution::scheduler`. |

## Member Types

### `resolve_query_t`

| Field | Type | Description |
|-------|------|-------------|
| `host_name` | `std::string` | Hostname or IP string. |
| `service_name` | `std::string` | Service name or port string. |
| `flags` | Bitmask | Resolution flags. |

**Flags:**

| Flag | Description |
|------|-------------|
| `passive` | `AI_PASSIVE` — address intended for `bind()`. |
| `canonical_name` | `AI_CANONNAME` — request canonical name. |
| `numeric_host` | `AI_NUMERICHOST` — don't resolve hostname. |
| `numeric_service` | `AI_NUMERICSERV` — don't resolve service name. |
| `v4_mapped` | `AI_V4MAPPED` — map v4 to v6 if needed. |
| `all_matching` | `AI_ALL` — return all matching addresses. |
| `address_configured` | `AI_ADDRCONFIG` — only return configured address types. |

### `resolve_result_t`

| Field | Type | Description |
|-------|------|-------------|
| `endpoint` | `coio::endpoint` | The resolved endpoint. |
| `canonical_name` | `std::string` | Canonical hostname (when `canonical_name` flag is set). |

## Member Functions

### Lifecycle

| Name | Description |
|------|-------------|
| `explicit basic_resolver(Scheduler)` | Constructs bound to a scheduler. |

### Observers

| Name | Description |
|------|-------------|
| `get_scheduler() -> Scheduler` | Returns the associated scheduler. |

### Synchronous Resolution

```cpp
static resolve(query_t) -> coio::generator<resolve_result_t>;
static resolve(Protocol, query_t) -> coio::generator<resolve_result_t>;
```

Returns a **generator** yielding resolved endpoints one at a time.

### Asynchronous Resolution

```cpp
async_resolve(query_t) -> sender;
async_resolve(Protocol, query_t) -> sender;
```

Returns a sender that runs DNS resolution on the associated scheduler.

## Example

```cpp
#include <coio/net/tcp.h>
#include <coio/net/resolver.h>

auto resolve_and_connect(io_context& ctx) -> coio::task<> {
    auto sched = ctx.get_scheduler();
    coio::tcp::resolver resolver{sched};

    coio::resolve_query_t query{
        .host_name = "example.com",
        .service_name = "80"
    };

    // Synchronous resolution via generator
    for (auto result : coio::tcp::resolver::resolve(coio::tcp::v4(), query)) {
        std::println("Resolved: {}", result.endpoint);
        // Try to connect...
    }
}
```

## Notes

- The resolver uses `getaddrinfo` under the hood on both Linux and Windows.
- Resolution is performed off the I/O thread to avoid blocking the event loop.
