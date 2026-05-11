#include <cstdint>
#include <doctest/doctest.h>
#include <coio/net/basic.h>

TEST_CASE("endpoint and byte-order helpers round-trip and compare") {
    CHECK(coio::net_to_host(coio::host_to_net(std::uint32_t{0x12345678})) == 0x12345678u);

    CHECK(coio::ipv4_address{"127.0.0.1"} == coio::ipv4_address::loopback());
    CHECK(coio::ipv4_address::any().to_string() == "0.0.0.0");
    CHECK(coio::ipv6_address{"::1"} == coio::ipv6_address::loopback());
    CHECK(coio::ipv6_address::any().to_string() == "::");

    coio::endpoint ep4{coio::ipv4_address::loopback(), 8080};
    CHECK(ep4.ip().is_v4());
    CHECK(ep4.port() == 8080);
    CHECK(ep4 == coio::endpoint{coio::ipv4_address::loopback(), 8080});
    CHECK(ep4 < coio::endpoint{coio::ipv4_address::loopback(), 8081});

    auto [ip, port] = ep4;
    CHECK(port == 8080);
    CHECK(ip == coio::ip_address{coio::ipv4_address::loopback()});
    CHECK(ep4.get<0>() == ip);
    CHECK(ep4.get<1>() == 8080);

    const auto ep6 = coio::endpoint{coio::ipv6_address::loopback(), 443};
    CHECK(ep6.ip().is_v6());
    CHECK(ep6.port() == 443);
    CHECK(ep6.get<0>().is_v6());
    CHECK(ep6.get<1>() == 443);
}
