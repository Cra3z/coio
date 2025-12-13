#pragma once
#include "socket.h"
#include "resolver.h"

namespace coio {
    class udp {
    public:
        template<io_scheduler IoScheduler>
        using socket = basic_datagram_socket<udp, IoScheduler>;

        using resolver = coio::resolver<udp>;

    private:
        explicit udp(int family) noexcept : family_(family) {}

    public:
        /**
         * \brief construct to represent the IPv4 UDP protocol.
         */
        udp() noexcept;

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
}