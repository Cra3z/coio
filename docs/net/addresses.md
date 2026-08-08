# Addresses & Endpoints

Value types for IP networking: `ipv4_address`, `ipv6_address`, the version-erased `ip_address`, and `endpoint` (address + port). All are small, trivially copyable value types with parsing, formatting, and ordering; `endpoint` additionally supports structured bindings.

Header: `#include <coio/net/basic.h>`

## Overview

| Type | Represents | Notable factories |
|------|-----------|-------------------|
| `ipv4_address` | 32-bit IPv4 address | `loopback()`, `any()` |
| `ipv6_address` | 128-bit IPv6 address | `loopback()`, `any()`, `v4_mapped(v4)` |
| `ip_address` | either of the above | implicit from `ipv4_address`/`ipv6_address` |
| `endpoint` | `ip_address` + 16-bit port | — |

All four are equality- and three-way-comparable and have `std::formatter` specializations, so they format directly with `std::format`.

## Synopsis

```cpp
namespace coio {
    class ipv4_address {
    public:
        ipv4_address() = default;                                   // 0.0.0.0
        explicit ipv4_address(std::uint32_t host_u32) noexcept;     // host byte order
        ipv4_address(const std::string& str);                       // parse "a.b.c.d"
        ipv4_address(std::uint8_t a, std::uint8_t b,
                     std::uint8_t c, std::uint8_t d) noexcept;

        auto to_string() const -> std::string;
        auto operator== (const ipv4_address&) const noexcept -> bool;
        auto operator<=>(const ipv4_address&) const noexcept -> std::strong_ordering;

        static auto loopback() noexcept -> ipv4_address;            // 127.0.0.1
        static auto any() noexcept -> ipv4_address;                 // 0.0.0.0
    };

    class ipv6_address {
    public:
        ipv6_address() = default;                                   // ::
        ipv6_address(const std::string& str);                       // parse

        auto to_string() const -> std::string;
        friend auto operator== (const ipv6_address&, const ipv6_address&) noexcept -> bool = default;
        friend auto operator<=>(const ipv6_address&, const ipv6_address&) noexcept = default;

        static auto loopback() noexcept -> ipv6_address;            // ::1
        static auto any() noexcept -> ipv6_address;                 // ::
        static auto v4_mapped(const ipv4_address&) noexcept -> ipv6_address; // ::ffff:a.b.c.d
    };

    class ip_address {
    public:
        ip_address() noexcept;                                      // ipv4_address::any()
        ip_address(const ipv4_address& v4) noexcept;                // implicit
        ip_address(const ipv6_address& v6) noexcept;                // implicit

        auto is_v4() const noexcept -> bool;
        auto is_v6() const noexcept -> bool;
        auto v4() const noexcept -> const ipv4_address&;
        auto v6() const noexcept -> const ipv6_address&;
        auto to_string() const -> std::string;

        friend auto operator== (const ip_address&, const ip_address&) noexcept -> bool;
        friend auto operator<=>(const ip_address&, const ip_address&) noexcept -> std::strong_ordering;
    };

    class endpoint {
    public:
        endpoint() = default;
        endpoint(const ipv4_address& addr, std::uint16_t port) noexcept;
        endpoint(const ipv6_address& addr, std::uint16_t port) noexcept;

        auto ip() noexcept -> ip_address&;
        auto ip() const noexcept -> const ip_address&;
        auto port() noexcept -> std::uint16_t&;
        auto port() const noexcept -> const std::uint16_t&;

        friend auto operator== (const endpoint&, const endpoint&) noexcept -> bool = default;
        friend auto operator<=>(const endpoint&, const endpoint&) noexcept -> std::strong_ordering = default;

        template<std::size_t I> requires (I < 2)
        decltype(auto) get() noexcept;              // structured-binding support
        template<std::size_t I> requires (I < 2)
        decltype(auto) get() const noexcept;
    };

    // byte-order helpers
    auto reverse_bytes(std::span<std::byte> bytes) noexcept -> void;
    inline constexpr auto host_to_net = /* T -> T, converts to network byte order */;
    inline constexpr auto net_to_host = /* T -> T, converts to host byte order */;
}

// tuple protocol for endpoint (enables structured bindings)
template<> struct std::tuple_size<coio::endpoint>;                  // 2
template<std::size_t I> struct std::tuple_element<I, coio::endpoint>;
// I == 0 -> coio::ip_address, I == 1 -> std::uint16_t

// formatting (when <format> is available)
template<> struct std::formatter<coio::ipv4_address>;
template<> struct std::formatter<coio::ipv6_address>;
template<> struct std::formatter<coio::ip_address>;
template<> struct std::formatter<coio::endpoint>;                    // "ip:port" / "[ip]:port"
```

## API Reference

### `ipv4_address`

- **`ipv4_address()`** — the all-zero address `0.0.0.0`.
- **`explicit ipv4_address(std::uint32_t host_u32)`** — from a 32-bit value in **host** byte order (`0x7f000001` is `127.0.0.1`); stored internally in network order.
- **`ipv4_address(const std::string& str)`** — parses dotted-decimal notation (`inet_pton`). Invalid input throws `std::invalid_argument`. Note this constructor is *implicit* from `std::string`, so a `std::string` value converts where an `ipv4_address` is expected; a string *literal* does not (that would take two user-defined conversions) — direct-initialize instead: `ipv4_address{"127.0.0.1"}`.
- **`ipv4_address(a, b, c, d)`** — from four octets, most significant first.
- **`to_string()`** — dotted-decimal form.
- **`loopback()`** — `127.0.0.1`; **`any()`** — `0.0.0.0` (bind-to-all).
- Comparison: `==` and `<=>` (strong ordering by address value).

### `ipv6_address`

- **`ipv6_address()`** — the unspecified address `::`.
- **`ipv6_address(const std::string& str)`** — parses standard IPv6 text form; invalid input throws `std::invalid_argument`. Implicit from `std::string`, like the IPv4 counterpart; a string literal still requires direct-initialization (`ipv6_address{"::1"}`).
- **`to_string()`** — canonical text form.
- **`loopback()`** — `::1`; **`any()`** — `::`.
- **`v4_mapped(v4)`** — the IPv4-mapped IPv6 address `::ffff:a.b.c.d` for `v4`. Useful with dual-stack (`v6_only{false}`) sockets.
- Comparison: defaulted `==` / `<=>` (lexicographic over the 16 bytes).

### `ip_address`

A tagged union of `ipv4_address` and `ipv6_address`.

- Default constructor yields `ipv4_address::any()`.
- Implicitly constructible from either concrete type, so APIs taking `ip_address` (e.g. `endpoint`) accept both.
- **`is_v4()` / `is_v6()`** — version query. **`v4()` / `v6()`** — access the stored value; calling the accessor that does not match the stored version is undefined (no check is performed).
- **`to_string()`** — delegates to the stored address.
- Comparison: addresses of different versions compare by version first (v4 < v6); same-version compares the underlying address.

### `endpoint`

An (`ip_address`, port) pair. The port is a plain `std::uint16_t` in host byte order.

- **`ip()` / `port()`** — both have const and non-const overloads; the non-const ones return mutable references, so `ep.port() = 8080;` is valid.
- Comparison: defaulted `==` / `<=>` (ip first, then port).
- **Structured bindings**: `endpoint` opts into the tuple protocol with element 0 = `ip_address`, element 1 = `std::uint16_t`:

```cpp
auto [ip, port] = socket.local_endpoint();
```

### Formatting

With `<format>` support, all four types have `std::formatter` specializations accepting an empty format spec. `endpoint` formats as `ip:port` for IPv4 and `[ip]:port` for IPv6:

```cpp
std::format("connected to {}", sock.remote_endpoint());   // e.g. "connected to 127.0.0.1:8086"
```

!!! note
    IPv6 endpoints are bracketed in the RFC 3986 form used by asio (e.g. `[::1]:8086`), so the port is unambiguous even though the address itself contains `:`.

### Byte-order helpers

- **`reverse_bytes(std::span<std::byte>)`** — reverses a byte range in place.
- **`host_to_net` / `net_to_host`** — function objects converting any trivially copyable value between host and network byte order (no-ops on big-endian hosts).

## Example

```cpp
#include <coio/net/basic.h>
#include <format>
#include <iostream>

auto main() -> int {
    coio::ipv4_address a{127, 0, 0, 1};
    coio::ipv4_address b{"127.0.0.1"};
    coio::ipv4_address c = coio::ipv4_address::loopback();
    // a == b == c

    coio::ipv6_address mapped = coio::ipv6_address::v4_mapped(a);   // ::ffff:127.0.0.1

    coio::endpoint server{coio::ipv4_address::any(), 8086};
    auto [ip, port] = server;                                       // structured bindings
    std::cout << std::format("{} -> ip={}, port={}\n", server, ip, port);

    coio::endpoint v6ep{coio::ipv6_address::loopback(), 8086};
    std::cout << std::format("v6: {}\n", v6ep);                     // "[::1]:8086"
}
```

## See also

- [Protocols](protocols.md) — `tcp` / `udp` descriptors choosing v4/v6
- [Sockets](sockets.md) — `bind`, `connect`, `local_endpoint`, `remote_endpoint`
- [Resolver](resolver.md) — turning host names into endpoints
