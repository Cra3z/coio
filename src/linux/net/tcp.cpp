#include <coio/net/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace coio {
    tcp::tcp() noexcept : tcp(AF_INET) {}

    auto tcp::v4() noexcept -> tcp {
        return tcp{AF_INET};
    }

    auto tcp::v6() noexcept -> tcp {
        return tcp{AF_INET6};
    }

    auto tcp::type() noexcept -> int {
        return SOCK_STREAM;
    }

    auto tcp::protocol_id() noexcept -> int {
        return IPPROTO_TCP;
    }

}