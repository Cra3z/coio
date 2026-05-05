# Address & Endpoint Types

**Header:** `<coio/net/basic.h>`

---

## `coio::ipv4_address`

Represents an IPv4 address.

### Constructors

| Name | Description |
|------|-------------|
| `ipv4_address()` | Default. Constructs `0.0.0.0`. |
| `explicit ipv4_address(uint32_t)` | Constructs from a host-order 32-bit integer. |
| `explicit ipv4_address(const std::string&)` | Parses from a dotted-decimal string. |
| `ipv4_address(uint8_t a, uint8_t b, uint8_t c, uint8_t d)` | Constructs from four octets. |

### Observers

| Name | Description |
|------|-------------|
| `to_string() const -> std::string` | Returns a dotted-decimal string. |

### Operators

| Name | Description |
|------|-------------|
| `operator==` | Equality comparison. |
| `operator<=>` | Three-way comparison (`std::strong_ordering`). |

### Static Members

| Name | Description |
|------|-------------|
| `static loopback() -> ipv4_address` | Returns `127.0.0.1`. |
| `static any() -> ipv4_address` | Returns `0.0.0.0`. |

---

## `coio::ipv6_address`

Represents an IPv6 address.

### Constructors

| Name | Description |
|------|-------------|
| `ipv6_address()` | Default. Constructs `::`. |
| `explicit ipv6_address(const std::string&)` | Parses from a colon-hex string. |

### Observers

| Name | Description |
|------|-------------|
| `to_string() const -> std::string` | Returns a colon-hex string. |

### Operators

| Name | Description |
|------|-------------|
| `operator==` | Equality comparison. |
| `operator<=>` | Three-way comparison. |

### Static Members

| Name | Description |
|------|-------------|
| `static loopback() -> ipv6_address` | Returns `::1`. |
| `static any() -> ipv6_address` | Returns `::`. |
| `static v4_mapped(ipv4_address) -> ipv6_address` | Converts to a v4-mapped IPv6 address. |

---

## `coio::ip_address`

A union type holding either an IPv4 or IPv6 address.

### Constructors

| Name | Description |
|------|-------------|
| `ip_address()` | Default. |
| `ip_address(ipv4_address)` | Constructs from IPv4. |
| `ip_address(ipv6_address)` | Constructs from IPv6. |

### Observers

| Name | Description |
|------|-------------|
| `is_v4() const -> bool` | Returns `true` if holding an IPv4 address. |
| `is_v6() const -> bool` | Returns `true` if holding an IPv6 address. |
| `v4() const -> const ipv4_address&` | Returns the IPv4 address. Precondition: `is_v4()`. |
| `v6() const -> const ipv6_address&` | Returns the IPv6 address. Precondition: `is_v6()`. |
| `to_string() const -> std::string` | Returns a string representation. |

### Operators

| Name | Description |
|------|-------------|
| `operator==` | Equality comparison. |
| `operator<=>` | Three-way comparison. |

---

## `coio::endpoint`

Represents an IP address + port pair.

### Constructors

| Name | Description |
|------|-------------|
| `endpoint()` | Default. |
| `endpoint(ipv4_address, uint16_t port)` | Constructs an IPv4 endpoint. |
| `endpoint(ipv6_address, uint16_t port)` | Constructs an IPv6 endpoint. |

### Observers

| Name | Description |
|------|-------------|
| `ip() -> ip_address&` | Returns a mutable reference to the address. |
| `ip() const -> const ip_address&` | Returns a const reference to the address. |
| `port() -> uint16_t&` | Returns a mutable reference to the port. |
| `port() const -> uint16_t` | Returns the port. |

### Operators

| Name | Description |
|------|-------------|
| `operator==` | Equality comparison. |
| `operator<=>` | Three-way comparison. |

### Tuple Protocol

`endpoint` supports structured bindings:

```cpp
auto [addr, port] = endpoint;

// std::get also works:
auto& addr = std::get<0>(endpoint);
auto& port = std::get<1>(endpoint);
```

## Free Functions

### Endian Conversion

```cpp
// Reverse bytes in-place
auto coio::reverse_bytes(std::span<std::byte>) -> void;

// Host-to-network byte order
template<std::integral T>
auto coio::host_to_net(T) -> T;

// Network-to-host byte order
template<std::integral T>
auto coio::net_to_host(T) -> T;
```

### Example

```cpp
#include <coio/net/basic.h>

auto addr = coio::ipv4_address{192, 168, 1, 1};
std::println("{}", addr.to_string()); // 192.168.1.1

coio::endpoint ep{addr, 8080};
auto [ip, port] = ep;
std::println("{}:{}", ip.to_string(), port); // 192.168.1.1:8080
```
