#pragma once
#include <cstdint>
#include <cstddef>
#include <compare>
#include <optional>
#if COIO_OS_WINSOWS
#include <BaseTsd.h>
#endif
#include "../generator.h"
#include "../utils/format.h"

namespace coio {
    class ipv4_address {
    public:

        ipv4_address() = default;

        explicit ipv4_address(std::uint32_t host_u32) noexcept;

        ipv4_address(const std::string& str);

        ipv4_address(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) noexcept : ipv4_address((std::uint32_t(a) << 24) | (std::uint32_t(b) << 16) | (std::uint32_t(c) << 8) | std::uint32_t(d)) {}

        [[nodiscard]]
        auto to_string() const -> std::string;

        auto operator== (const ipv4_address& other) const noexcept -> bool;

        auto operator<=> (const ipv4_address& other) noexcept -> std::strong_ordering;

        [[nodiscard]]
        static auto loopback() noexcept ->ipv4_address {
            return ipv4_address{0x7f000001u};
        }

        [[nodiscard]]
        static auto any() noexcept ->ipv4_address {
            return {};
        }

    private:
        std::uint32_t net_u32_ = 0;
    };

    class ipv6_address {
    public:
        ipv6_address() = default;

        ipv6_address(const std::string& str);

        [[nodiscard]]
        auto to_string() const -> std::string;

        friend auto operator== (const ipv6_address& lhs, const ipv6_address& rhs) noexcept -> bool = default;

        friend auto operator<=> (const ipv6_address& lhs, const ipv6_address& rhs) noexcept = default;

        [[nodiscard]]
        static auto loopback() noexcept ->ipv6_address {
            ipv6_address result;
            result.val_[15] = std::byte(1);
            return result;
        }

        [[nodiscard]]
        static auto any() noexcept ->ipv6_address {
            return {};
        }

        [[nodiscard]]
        static auto v4_mapped(const ipv4_address& ipv4) noexcept -> ipv6_address {
            ipv6_address result;
            result.val_[10] = result.val_[11] = std::byte(0xff);
            auto ipv4_bytes = reinterpret_cast<const std::byte*>(&ipv4);
            for (std::size_t i = 0; i < 4; ++i) result.val_[12 + i] = ipv4_bytes[i];
            return result;
        }

    private:
        alignas(4) std::byte val_[16]{};
    };


    class ip_address {
    public:
        ip_address(const ipv4_address& v4) noexcept : v4_(v4), version_(4) {}

        ip_address(const ipv6_address& v6) noexcept : v6_(v6), version_(6) {}

        [[nodiscard]]
        auto is_v4() const noexcept -> bool {
            return version_ == 4;
        }

        [[nodiscard]]
        auto is_v6() const noexcept -> bool {
            return version_ == 6;
        }

        [[nodiscard]]
        auto v4() const noexcept -> const ipv4_address& {
            return v4_;
        }

        [[nodiscard]]
        auto v6() const noexcept -> const ipv6_address& {
            return v6_;
        }

        [[nodiscard]]
        auto to_string() const -> std::string {
            return is_v4() ? v4_.to_string() : v6_.to_string();
        }

        friend auto operator== (const ip_address& lhs, const ip_address& rhs) noexcept -> bool {
            if (lhs.version_ != rhs.version_) return false;
            if (lhs.is_v4()) return lhs.v4() == rhs.v4();
            return lhs.v6() == rhs.v6();
        }

    private:
        union {
            ipv4_address v4_;
            ipv6_address v6_;
        };
        std::uint8_t version_;
    };


    class endpoint {
    public:

        endpoint(const ipv4_address& ipv4_addr, std::uint16_t port) noexcept : ip_(ipv4_addr), port_(port) {}

        endpoint(const ipv6_address& ipv6_addr, std::uint16_t port) noexcept : ip_(ipv6_addr), port_(port) {}

        [[nodiscard]]
        auto ip() noexcept -> ip_address& {
            return ip_;
        }

        [[nodiscard]]
        auto ip() const noexcept -> const ip_address& {
            return ip_;
        }

        [[nodiscard]]
        auto port() noexcept -> std::uint16_t& {
            return port_;
        }

        [[nodiscard]]
        auto port() const noexcept -> const std::uint16_t& {
            return port_;
        }

        friend auto operator== (const endpoint& lhs, const endpoint& rhs) noexcept -> bool = default;

        friend auto operator<=> (const endpoint& lhs, const endpoint& rhs) noexcept -> std::strong_ordering = default;

        template<std::size_t I> requires (I < 2)
        decltype(auto) get() noexcept {
            if constexpr (I == 0) {
                return ip();
            }
            else {
                return port();
            }
        }

        template<std::size_t I> requires (I < 2)
        decltype(auto) get() const noexcept {
            if constexpr (I == 0) {
                return ip();
            }
            else {
                return port();
            }
        }

    private:
        ip_address ip_;
        std::uint16_t port_;
    };


    class tcp {
    private:
        explicit tcp(int family) noexcept : family_(family) {}

    public:

        /**
         * \brief construct to represent the IPv4 TCP protocol.
         */
        [[nodiscard]]
        static auto v4() noexcept -> tcp;

        /**
         * \brief construct to represent the IPv6 TCP protocol.
         */
        [[nodiscard]]
        static auto v6() noexcept -> tcp;

        /**
         * \brief get the identifier for the protocol family.
         */
        [[nodiscard]]
        auto family() const noexcept -> int {
            return family_;
        }

        /**
         * \brief get the identifier for the type of the protocol.
         */
        [[nodiscard]]
        static auto type() noexcept -> int;

        /**
         * \brief get the identifier for the protocol.
         */
        [[nodiscard]]
        static auto protocol_id() noexcept -> int;

    private:
        int family_;
    };

    class udp {
    private:
        explicit udp(int family) noexcept : family_(family) {}

    public:

        /**
         * \brief construct to represent the IPv4 UDP protocol.
         */
        [[nodiscard]]
        static auto v4() noexcept -> udp;

        /**
         * \brief construct to represent the IPv6 UDP protocol.
         */
        [[nodiscard]]
        static auto v6() noexcept -> udp;

        /**
         * \brief get the identifier for the protocol family.
         */
        [[nodiscard]]
        auto family() const noexcept -> int {
            return family_;
        }

        /**
         * \brief get the identifier for the type of the protocol.
         */
        [[nodiscard]]
        static auto type() noexcept -> int;

        /**
         * \brief get the identifier for the protocol.
         */
        [[nodiscard]]
        static auto protocol_id() noexcept -> int;

    private:
        int family_;
    };

    inline auto reverse_bytes(std::span<std::byte> bytes) noexcept -> void  {
        if (bytes.empty()) [[unlikely]] return;
        for (std::size_t i = 0, j = bytes.size() - 1; i < j; ++i, --j) {
            std::swap(bytes[i], bytes[j]);
        }
    }

    inline constexpr auto host_to_net = []<typename T> requires std::is_trivially_copyable_v<T> (T in_host) noexcept -> T {
        static_assert(std::endian::native == std::endian::little or std::endian::native == std::endian::big);
        if constexpr (std::endian::native == std::endian::little) {
            (reverse_bytes)(std::span{reinterpret_cast<std::byte*>(&in_host), sizeof(T)});
        }
        return in_host;
    };

    inline constexpr auto net_to_host = []<typename T> requires std::is_trivially_copyable_v<T> (T from_net) noexcept -> T {
        static_assert(std::endian::native == std::endian::little or std::endian::native == std::endian::big);
        if constexpr (std::endian::native == std::endian::little) {
            (reverse_bytes)(std::span{reinterpret_cast<std::byte*>(&from_net), sizeof(T)});
        }
        return from_net;
    };


    namespace detail {
#if COIO_OS_LINUX
        using socket_native_handle_type = int;
#elif COIO_OS_WINDOWS
        using socket_native_handle_type = ::UINT_PTR;
#endif
        inline constexpr socket_native_handle_type invalid_socket_handle_value = socket_native_handle_type(-1);

        struct resolve_query_t {
            static const int canonical_name;
            static const int passive;
            static const int numeric_host;
            static const int numeric_service;
            static const int v4_mapped;
            static const int all_matching;
            static const int address_configured;

            std::string host_name;
            std::string service_name;
            int         flags{v4_mapped | address_configured};
        };

        struct resolve_result_t {
            class endpoint endpoint;
            std::string canonical_name;
        };

        auto resolve_impl(resolve_query_t query, int family, int socktype, int protocol_id) -> generator<resolve_result_t>;

        auto resolve_impl(resolve_query_t query, int sock_type, int protocol_id) -> generator<resolve_result_t>;

    }

    template<typename Protocol>
    class resolver {
    public:
        using protocol_type = Protocol;
        using query_t = detail::resolve_query_t;
        using result_t = detail::resolve_result_t;

    public:
        resolver() = default;

        explicit resolver(protocol_type protocol) noexcept(std::is_nothrow_move_constructible_v<protocol_type>) : protocol_(std::move(protocol)) {}

        /**
         * \brief resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        auto resolve(query_t query) const -> generator<result_t> {
            if (protocol_) return detail::resolve_impl(std::move(query), protocol_->family(), protocol_type::type(), protocol_type::protocol_id());
            return detail::resolve_impl(std::move(query), protocol_type::type(), protocol_type::protocol_id());
        }

    private:
        std::optional<protocol_type> protocol_;
    };

    using tcp_resolver = resolver<tcp>;
    using udp_resolver = resolver<udp>;
}

template<>
struct std::tuple_size<coio::endpoint> : std::integral_constant<std::size_t, 2> {};

template<std::size_t I>
struct std::tuple_element<I, coio::endpoint> {
    using type = std::conditional_t<I == 0, coio::ip_address, std::uint16_t>;
};

#ifdef __cpp_lib_format

template<>
struct std::formatter<coio::ipv4_address> : coio::no_specification_formatter {
    auto format(const coio::ipv4_address& ipv4, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", ipv4.to_string());
    }
};


template<>
struct std::formatter<coio::ipv6_address> : coio::no_specification_formatter {
    auto format(const coio::ipv6_address& ipv6, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", ipv6.to_string());
    }
};


template<>
struct std::formatter<coio::ip_address> : coio::no_specification_formatter {
    auto format(const coio::ip_address& ip, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", ip.to_string());
    }
};


template<>
struct std::formatter<coio::endpoint> : coio::no_specification_formatter {
    auto format(const coio::endpoint& ep, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}:{}", ep.ip().to_string(), ep.port());
    }
};

#endif