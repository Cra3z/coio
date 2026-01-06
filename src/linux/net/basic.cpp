#include <bit>
#include <cstring>
#include <variant>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <coio/detail/error.h>
#include <coio/net/basic.h>
#include <coio/utils/scope_exit.h>

namespace coio {
    auto error::gai_category_t::message(int ec) const -> std::string {
        return ::gai_strerror(ec);
    }

    ipv4_address::ipv4_address(std::uint32_t host_u32) noexcept : net_u32_(::htonl(host_u32)) {}

    ipv4_address::ipv4_address(const std::string& str) {
        if (::inet_pton(AF_INET, str.c_str(), &net_u32_) == -1) {
            throw std::invalid_argument{"invalid ipv4 network address in dotted-decimal format."};
        }
    }

    auto ipv4_address::to_string() const -> std::string {
        char buf[INET_ADDRSTRLEN];
        return ::inet_ntop(AF_INET, &net_u32_, buf, INET_ADDRSTRLEN);
    }

    auto ipv4_address::operator==(const ipv4_address& other) const noexcept -> bool {
        return ::ntohl(net_u32_) == ::ntohl(other.net_u32_);
    }

    auto ipv4_address::operator<=>(const ipv4_address& other) const noexcept -> std::strong_ordering {
        return ::ntohl(net_u32_) <=> ::ntohl(other.net_u32_);
    }

    ipv6_address::ipv6_address(const std::string& str) {
        if (::inet_pton(AF_INET6, str.c_str(), val_) == -1) {
            throw std::invalid_argument{"invalid format for ipv6 network address."};
        }
    }

    auto ipv6_address::to_string() const -> std::string {
        char buf[INET6_ADDRSTRLEN];
        return ::inet_ntop(AF_INET6, val_, buf, INET6_ADDRSTRLEN);
    }


    namespace detail {
        auto endpoint_to_sockaddr_in(const endpoint& addr) noexcept -> std::variant<::sockaddr_in, ::sockaddr_in6> {
            if (addr.ip().is_v4()) {
                return ::sockaddr_in{
                    .sin_family = AF_INET,
                    .sin_port = ::htons(addr.port()),
                    .sin_addr = std::bit_cast<::in_addr>(addr.ip().v4())
                };
            }
            return ::sockaddr_in6{
                .sin6_family = AF_INET6,
                .sin6_port = ::htons(addr.port()),
                .sin6_addr = std::bit_cast<::in6_addr>(addr.ip().v6())
            };
        }

        auto sockaddr_to_endpoint(::sockaddr* sa) noexcept -> endpoint  {
            switch (sa->sa_family) {
            case AF_INET: {
                auto ipv4 = reinterpret_cast<::sockaddr_in*>(sa);
                return endpoint{std::bit_cast<ipv4_address>(ipv4->sin_addr), ::ntohs(ipv4->sin_port)};
            }
            case AF_INET6: {
                auto ipv6 = reinterpret_cast<::sockaddr_in6*>(sa);
                return endpoint{std::bit_cast<ipv6_address>(ipv6->sin6_addr), ::ntohs(ipv6->sin6_port)};
            }
            default: unreachable();
            }
        }

        auto sockaddr_storage_to_endpoint(::sockaddr_storage& addr) noexcept -> endpoint {
            switch (addr.ss_family) {
            case AF_INET: {
                ::sockaddr_in ipv4{};
                std::memcpy(&ipv4, &addr, sizeof(ipv4));
                return {std::bit_cast<ipv4_address>(ipv4.sin_addr), ::ntohs(ipv4.sin_port)};
            }
            case AF_INET6: {
                ::sockaddr_in6 ipv6{};
                std::memcpy(&ipv6, &addr, sizeof(ipv6));
                return {std::bit_cast<ipv6_address>(ipv6.sin6_addr), ::ntohs(ipv6.sin6_port)};
            }
            default: unreachable();
            }
        }

        auto to_sockaddr(std::variant<::sockaddr_in, ::sockaddr_in6>& sa) -> std::pair<::sockaddr*, ::socklen_t> {
            return std::visit([](auto& sai) noexcept -> std::pair<::sockaddr*, ::socklen_t> {
                return {reinterpret_cast<::sockaddr*>(&sai), sizeof(sai)};
            }, sa);
        }
    }
}