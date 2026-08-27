#include <cstdint>
#include <format>
#include <iomanip>
#include <sstream>
#include <string>
#include <doctest/doctest.h>
#include <coio/net/basic.h>

namespace {
    template<typename T>
    auto sstream_result(const T& value) -> std::string {
        std::ostringstream oss;
        oss << value;
        return std::move(oss).str();
    }
}

TEST_CASE("endpoint and byte-order helpers round-trip and compare") {
    CHECK_EQ(coio::net_to_host(coio::host_to_net(std::uint32_t{0x12345678})), 0x12345678u);

    CHECK_EQ(coio::ipv4_address{"127.0.0.1"}, coio::ipv4_address::loopback());
    CHECK_EQ(coio::ipv4_address::any().to_string(), "0.0.0.0");
    CHECK_EQ(coio::ipv6_address{"::1"}, coio::ipv6_address::loopback());
    CHECK_EQ(coio::ipv6_address::any().to_string(), "::");

    coio::endpoint ep4{coio::ipv4_address::loopback(), 8080};
    CHECK(ep4.ip().is_v4());
    CHECK_EQ(ep4.port(), 8080);
    CHECK_EQ(ep4, coio::endpoint{coio::ipv4_address::loopback(), 8080});
    CHECK_LE(ep4, coio::endpoint{coio::ipv4_address::loopback(), 8081});

    auto [ip, port] = ep4;
    CHECK_EQ(port, 8080);
    CHECK_EQ(ip, coio::ip_address{coio::ipv4_address::loopback()});
    CHECK_EQ(ep4.get<0>(), ip);
    CHECK_EQ(ep4.get<1>(), 8080);

    const auto ep6 = coio::endpoint{coio::ipv6_address::loopback(), 443};
    CHECK(ep6.ip().is_v6());
    CHECK_EQ(ep6.port(), 443);
    CHECK(ep6.get<0>().is_v6());
    CHECK_EQ(ep6.get<1>(), 443);
}

TEST_CASE("endpoint formatting brackets IPv6 addresses") {
    const coio::endpoint ep4{coio::ipv4_address::loopback(), 8080};
    CHECK_EQ(std::format("{}", ep4), "127.0.0.1:8080");
    CHECK_EQ(ep4.to_string(), "127.0.0.1:8080");

    const coio::endpoint ep6{coio::ipv6_address::loopback(), 443};
    CHECK_EQ(std::format("{}", ep6), "[::1]:443");
    CHECK_EQ(ep6.to_string(), "[::1]:443");

    const coio::endpoint ep6_any{coio::ipv6_address::any(), 0};
    CHECK_EQ(std::format("{}", ep6_any), "[::]:0");
    CHECK_EQ(ep6_any.to_string(), "[::]:0");
}

TEST_CASE("output addresses and endpoints to ostream") {
    CHECK_EQ(sstream_result(coio::ipv4_address::loopback()), "127.0.0.1");
    CHECK_EQ(sstream_result(coio::ipv4_address::any()), "0.0.0.0");
    CHECK_EQ(sstream_result(coio::ipv6_address::loopback()), "::1");
    CHECK_EQ(sstream_result(coio::ipv6_address::any()), "::");

    CHECK_EQ(sstream_result(coio::ip_address{coio::ipv4_address::loopback()}), "127.0.0.1");
    CHECK_EQ(sstream_result(coio::ip_address{coio::ipv6_address::loopback()}), "::1");

    CHECK_EQ(sstream_result(coio::endpoint{coio::ipv4_address::loopback(), 8080}), "127.0.0.1:8080");
    CHECK_EQ(sstream_result(coio::endpoint{coio::ipv6_address::loopback(), 443}), "[::1]:443");

    std::ostringstream oss;
    oss << std::setw(12) << std::setfill('.') << coio::ipv4_address::loopback();
    CHECK_EQ(std::move(oss).str(), "...127.0.0.1");
}
