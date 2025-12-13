#pragma once
#include "../net/basic.h"

namespace coio::detail {
    struct async_receive_t {
        using result_type = std::size_t;
        std::span<std::byte> buffer;
    };

    struct async_send_t {
        using result_type = std::size_t;
        std::span<const std::byte> buffer;
    };

    struct async_receive_from_t {
        using result_type = std::size_t;
        std::span<std::byte> buffer;
        endpoint peer;
    };

    struct async_send_to_t {
        using result_type = std::size_t;
        std::span<const std::byte> buffer;
        endpoint peer;
    };

    struct async_accept_t {
        using result_type = socket_native_handle_type;
    };

    struct async_connect_t {
        using result_type = void;
        endpoint peer;
    };
}