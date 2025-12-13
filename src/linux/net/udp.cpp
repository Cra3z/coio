#include <coio/net/udp.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace coio {
    udp::udp() noexcept : udp(AF_INET) {}

    auto udp::v4() noexcept -> udp {
        return udp{AF_INET};
    }

    auto udp::v6() noexcept -> udp {
        return udp{AF_INET6};
    }

    auto udp::type() noexcept -> int {
        return SOCK_DGRAM;
    }

    auto udp::protocol_id() noexcept -> int {
        return IPPROTO_UDP;
    }
}