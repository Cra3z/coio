# Resolver

`basic_resolver<Protocol, Scheduler>` turns host and service names into [`endpoint`](addresses.md)s, wrapping the OS resolver (`getaddrinfo`). Results come back as a `coio::generator` walking an already-materialized result list; the `async_resolve` form re-schedules onto the resolver's scheduler and performs the blocking lookup there. Use it through the protocol aliases `coio::tcp::resolver<S>` / `coio::udp::resolver<S>`.

Header: `#include <coio/net/resolver.h>`

## Overview

- `resolve_query_t` — what to look up: `host_name`, `service_name`, and `AI_*`-style flags.
- `resolve_result_t` — one result: an `endpoint` plus (optionally) the canonical host name.
- `basic_resolver` — `resolve` (synchronous, static) and `async_resolve` (sender-returning) in protocol-restricted form.

The resolver requires only a plain `std::execution` **scheduler** (`coio::scheduler` concept), not an I/O scheduler — it performs no socket I/O of its own. The `Protocol` parameter (e.g. `coio::tcp`) restricts results to that protocol's socket type and protocol number.

!!! note "Where the blocking lookup runs"
    coio wraps the operating system's blocking `getaddrinfo`. The synchronous `resolve` performs
    the lookup during the call itself, blocking the calling thread. `async_resolve` offloads it:
    the lookup runs on the thread the resolver's scheduler dispatches to, so the initiating
    thread stays free — but the scheduler's consumer thread is occupied for the duration of the
    lookup. Give the resolver a dedicated context if lookups are frequent or slow.

## Synopsis

```cpp
namespace coio {
    struct resolve_query_t {
        // flag constants (values of the platform's AI_* flags)
        static const int canonical_name;      // AI_CANONNAME
        static const int passive;             // AI_PASSIVE
        static const int numeric_host;        // AI_NUMERICHOST
        static const int numeric_service;     // AI_NUMERICSERV
        static const int v4_mapped;           // AI_V4MAPPED
        static const int all_matching;        // AI_ALL
        static const int address_configured;  // AI_ADDRCONFIG

        std::string host_name;
        std::string service_name;
        int         flags{v4_mapped | address_configured};
    };

    struct resolve_result_t {
        endpoint    endpoint;
        std::string canonical_name;
    };

    template<typename Protocol, scheduler Scheduler>
    class basic_resolver {
    public:
        using protocol_type = Protocol;
        using scheduler_type = Scheduler;
        using query_t  = resolve_query_t;
        using result_t = resolve_result_t;

        explicit basic_resolver(Scheduler sched) noexcept;

        auto get_scheduler() const noexcept -> Scheduler;

        static auto resolve(query_t query) -> generator<result_t>;
        static auto resolve(const protocol_type& protocol, query_t query) -> generator<result_t>;

        auto async_resolve(query_t query) const;                          // sender of generator<result_t>
        auto async_resolve(protocol_type protocol, query_t query) const;  // sender of generator<result_t>
    };

    // protocol aliases (in <coio/net/tcp.h> / <coio/net/udp.h>)
    // tcp::resolver<Scheduler> = basic_resolver<tcp, Scheduler>;
    // udp::resolver<Scheduler> = basic_resolver<udp, Scheduler>;
}
```

## API Reference

### `resolve_query_t`

| Member | Meaning |
|--------|---------|
| `host_name` | Host to resolve (name or numeric address). Empty means "no host": with `passive` you get wildcard (bind-to-any) endpoints, otherwise loopback. |
| `service_name` | Service name (`"http"`) or numeric port (`"8086"`). May be empty (port 0). |
| `flags` | Bitwise-OR of the flag constants; defaults to `v4_mapped \| address_configured`. |

Flag semantics (mirroring `getaddrinfo`):

| Flag | Effect |
|------|--------|
| `canonical_name` | Fill `resolve_result_t::canonical_name` (first entry). |
| `passive` | Endpoints intended for `bind()`/`listen()`; empty host yields the wildcard address. |
| `numeric_host` | `host_name` must already be a numeric address; never consult DNS. |
| `numeric_service` | `service_name` must be a numeric port string. |
| `v4_mapped` | If no IPv6 addresses are found, return IPv4-mapped IPv6 addresses (for `AF_INET6` queries). *(default)* |
| `all_matching` | With `v4_mapped`: return both IPv6 and mapped-IPv4 addresses. |
| `address_configured` | Only return address families configured on a local interface. *(default)* |

### `resolve_result_t`

One resolution entry: `endpoint` (address + port, ready to pass to `connect`/`bind`) and `canonical_name` (non-empty only for the entry carrying the canonical name when `canonical_name` was requested; otherwise `""`).

### `basic_resolver(Scheduler sched)` / `get_scheduler()`
Stores the scheduler used by `async_resolve`. `resolve` is static and does not need an instance.

### `resolve(query_t query) -> generator<result_t>` *(static)*

Resolves with the protocol's socket type and protocol number, any address family (`AF_UNSPEC`).

- **Eager**: the blocking `getaddrinfo` lookup runs during the `resolve(...)` call itself, on the calling thread. The returned [`generator`](../coroutines/generator.md) lazily walks the already-materialized results — iterating it never blocks on the OS resolver and never throws lookup errors.
- Throws `std::system_error` with `coio::error::gai_category()` **at the call** if the lookup fails; the error message is the OS's `gai_strerror` text.

### `resolve(const protocol_type& protocol, query_t query) -> generator<result_t>` *(static)*

As above, additionally restricted to `protocol.family()` — e.g. `tcp::v6()` yields only IPv6 endpoints.

### `async_resolve(query_t query) const` / `async_resolve(protocol_type protocol, query_t query) const`

Return a sender that re-schedules onto the resolver's scheduler, performs the blocking lookup on the thread the scheduler dispatches to, and completes there with `set_value(generator<result_t>)`. Lookup failure completes with `set_error(std::exception_ptr)` — holding the `std::system_error` with `coio::error::gai_category()` — also on the scheduler. Awaiting the sender from a task therefore does not block the task's thread during the lookup; iterating the delivered generator afterwards is a cheap, non-blocking walk of the ready results.

### Thread safety

`basic_resolver` holds only a scheduler copy; distinct calls are independent. Each returned `generator` is single-pass and must be iterated by one thread at a time.

## Example

```cpp
#include <coio/core.h>
#include <coio/net/tcp.h>
#include <coio/net/socket.h>
#include <format>
#include <iostream>

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_resolver = coio::tcp::resolver<io_context::scheduler>;
using tcp_socket   = coio::tcp::socket<io_context::scheduler>;

auto connect_to(std::string host, std::string service) -> io_context::task<tcp_socket> {
    auto sched = co_await coio::read_scheduler();

    tcp_resolver resolver{sched};
    auto results = co_await resolver.async_resolve({
        .host_name = std::move(host),
        .service_name = std::move(service),
        .flags = tcp_resolver::query_t::canonical_name
                 | tcp_resolver::query_t::address_configured
    });

    for (auto&& [ep, canonical] : results) {           // lookup already ran on the scheduler; this walk never blocks
        std::clog << std::format("candidate: {} {}\n", ep, canonical);
        tcp_socket socket{sched};
        try {
            socket.open(ep.ip().is_v4() ? coio::tcp::v4() : coio::tcp::v6());
            co_await socket.async_connect(ep);
            co_return socket;                           // first endpoint that connects wins
        }
        catch (const std::system_error&) { /* try the next entry */ }
    }
    throw std::runtime_error{"no endpoint reachable"};
}
```

## See also

- [Addresses & Endpoints](addresses.md) — the `endpoint` results
- [Protocols](protocols.md) — `tcp::resolver` / `udp::resolver` aliases
- [Sockets](sockets.md) — connecting to resolved endpoints
- [generator](../coroutines/generator.md) — the lazy result range
- [Error handling](../error-handling.md) — `gai_category`
