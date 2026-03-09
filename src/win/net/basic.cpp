#include <bit>
#include <cstring>
#include <variant>
#include <mutex>
#include <coio/net/basic.h>
#include <coio/detail/error.h>
#include <coio/utils/scope_exit.h>
#include "../common.h"

namespace coio {
    auto error::gai_category_t::message(int ec) const -> std::string {
        return ::gai_strerror(ec);
    }

    ipv4_address::ipv4_address(std::uint32_t host_u32) noexcept
        : net_u32_(::htonl(host_u32)) {}

    ipv4_address::ipv4_address(const std::string& str) {
        if (::inet_pton(AF_INET, str.c_str(), &net_u32_) != 1) {
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
        if (::inet_pton(AF_INET6, str.c_str(), val_) != 1) {
            throw std::invalid_argument{"invalid format for ipv6 network address."};
        }
    }

    auto ipv6_address::to_string() const -> std::string {
        char buf[INET6_ADDRSTRLEN];
        return ::inet_ntop(AF_INET6, val_, buf, INET6_ADDRSTRLEN);
    }

    namespace detail {
        auto endpoint_to_sockaddr_in(const endpoint& ep) noexcept
            -> std::variant<SOCKADDR_IN, SOCKADDR_IN6>
        {
            if (ep.ip().is_v4()) {
                SOCKADDR_IN sa{};
                sa.sin_family = AF_INET;
                sa.sin_port   = ::htons(ep.port());
                sa.sin_addr   = std::bit_cast<::in_addr>(ep.ip().v4());
                return sa;
            }
            SOCKADDR_IN6 sa{};
            sa.sin6_family = AF_INET6;
            sa.sin6_port   = ::htons(ep.port());
            sa.sin6_addr   = std::bit_cast<::in6_addr>(ep.ip().v6());
            return sa;
        }

        auto sockaddr_to_endpoint(SOCKADDR* sa) noexcept -> endpoint {
            switch (sa->sa_family) {
            case AF_INET: {
                auto* ipv4 = reinterpret_cast<SOCKADDR_IN*>(sa);
                return endpoint{std::bit_cast<ipv4_address>(ipv4->sin_addr),
                                ::ntohs(ipv4->sin_port)};
            }
            case AF_INET6: {
                auto* ipv6 = reinterpret_cast<SOCKADDR_IN6*>(sa);
                return endpoint{std::bit_cast<ipv6_address>(ipv6->sin6_addr),
                                ::ntohs(ipv6->sin6_port)};
            }
            default: unreachable();
            }
        }

        auto sockaddr_storage_to_endpoint(SOCKADDR_STORAGE& addr) noexcept -> endpoint {
            switch (addr.ss_family) {
            case AF_INET: {
                SOCKADDR_IN ipv4{};
                std::memcpy(&ipv4, &addr, sizeof(ipv4));
                return {std::bit_cast<ipv4_address>(ipv4.sin_addr),
                        ::ntohs(ipv4.sin_port)};
            }
            case AF_INET6: {
                SOCKADDR_IN6 ipv6{};
                std::memcpy(&ipv6, &addr, sizeof(ipv6));
                return {std::bit_cast<ipv6_address>(ipv6.sin6_addr),
                        ::ntohs(ipv6.sin6_port)};
            }
            default: unreachable();
            }
        }

        auto to_sockaddr(std::variant<SOCKADDR_IN, SOCKADDR_IN6>& sa) noexcept
            -> std::pair<SOCKADDR*, int>
        {
            return std::visit([](auto& s) noexcept -> std::pair<SOCKADDR*, int> {
                return {reinterpret_cast<SOCKADDR*>(&s), static_cast<int>(sizeof(s))};
            }, sa);
        }

        // ---- Extension function pointer cache ----
        namespace {
            LPFN_ACCEPTEX              s_acceptex      = nullptr;
            LPFN_CONNECTEX             s_connectex     = nullptr;
            LPFN_GETACCEPTEXSOCKADDRS  s_getsockaddrs  = nullptr;
            std::once_flag             s_fn_once;

            void load_extension_fns() noexcept {
                // Use a temporary IPv4 TCP socket to retrieve the pointers.
                SOCKET tmp = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                    nullptr, 0, WSA_FLAG_OVERLAPPED);
                if (tmp == INVALID_SOCKET) return;

                DWORD bytes = 0;

                { GUID g = WSAID_ACCEPTEX;
                  ::WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &g, sizeof(g), &s_acceptex, sizeof(s_acceptex), &bytes,
                      nullptr, nullptr); }
                { GUID g = WSAID_CONNECTEX;
                  ::WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &g, sizeof(g), &s_connectex, sizeof(s_connectex), &bytes,
                      nullptr, nullptr); }
                { GUID g = WSAID_GETACCEPTEXSOCKADDRS;
                  ::WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &g, sizeof(g), &s_getsockaddrs, sizeof(s_getsockaddrs), &bytes,
                      nullptr, nullptr); }

                ::closesocket(tmp);
            }
        }

        auto get_acceptex_fn() noexcept -> LPFN_ACCEPTEX {
            std::call_once(s_fn_once, load_extension_fns);
            return s_acceptex;
        }

        auto get_connectex_fn() noexcept -> LPFN_CONNECTEX {
            std::call_once(s_fn_once, load_extension_fns);
            return s_connectex;
        }

        auto get_acceptex_sockaddrs_fn() noexcept -> LPFN_GETACCEPTEXSOCKADDRS {
            std::call_once(s_fn_once, load_extension_fns);
            return s_getsockaddrs;
        }
    } // namespace detail
} // namespace coio
