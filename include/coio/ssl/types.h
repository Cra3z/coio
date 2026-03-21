#pragma once
#include "../utils/utility.h"

namespace coio::ssl {
    enum class method {
        sslv23,
        sslv23_client,
        sslv23_server,
        tls,
        tls_client,
        tls_server,
    };

    enum class verify_mode : int {
        none = 0x00,
        peer = 0x01,
        fail_if_no_peer_cert = 0x02,
        client_once = 0x04,
    };

    enum class file_format : int {
        pem = 1,
        asn1 = 2,
    };

    enum class handshake_type : unsigned char {
        client,
        server,
    };

    [[nodiscard]]
    constexpr auto operator| (verify_mode lhs, verify_mode rhs) noexcept -> verify_mode {
        return static_cast<verify_mode>(to_underlying(lhs) | to_underlying(rhs));
    }

    [[nodiscard]]
    constexpr auto operator& (verify_mode lhs, verify_mode rhs) noexcept -> verify_mode {
        return static_cast<verify_mode>(to_underlying(lhs) & to_underlying(rhs));
    }
}