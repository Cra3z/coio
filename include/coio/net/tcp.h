#pragma once
#include "socket.h"
#include "resolver.h"

namespace coio {
    class tcp {
    public:
        template<io_scheduler IoScheduler>
        using acceptor = basic_socket_acceptor<tcp, IoScheduler>;

        template<io_scheduler IoScheduler>
        using socket = basic_stream_socket<tcp, IoScheduler>;

        using resolver = coio::resolver<tcp>;

    private:
        explicit tcp(int family) noexcept : family_(family) {}

    public:
        /**
         * \brief construct to represent the IPv4 TCP protocol.
         */
        tcp() noexcept;

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
}