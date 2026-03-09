#pragma once
#include "execution.h"
#include "../net/basic.h"

namespace coio::detail {
    struct async_read_some_t {
        using value_signature = execution::set_value_t(std::size_t);
        std::span<std::byte> buffer;
    };

    struct async_write_some_t {
        using value_signature = execution::set_value_t(std::size_t);
        std::span<const std::byte> buffer;
    };

    struct async_read_some_at_t {
        using value_signature = execution::set_value_t(std::size_t);
        std::size_t offset = 0;
        std::span<std::byte> buffer;
    };

    struct async_write_some_at_t {
        using value_signature = execution::set_value_t(std::size_t);
        std::size_t offset = 0;
        std::span<const std::byte> buffer;
    };

    struct async_receive_t {
        using value_signature = execution::set_value_t(std::size_t);
        std::span<std::byte> buffer;
    };

    struct async_send_t {
        using value_signature = execution::set_value_t(std::size_t);
        std::span<const std::byte> buffer;
    };

    struct async_receive_from_t {
        using value_signature = execution::set_value_t(endpoint, std::size_t);
        std::span<std::byte> buffer;
    };

    struct async_send_to_t {
        using value_signature = execution::set_value_t(std::size_t);
        std::span<const std::byte> buffer;
        endpoint peer;
    };

    struct async_accept_t {
        using value_signature = execution::set_value_t(socket_native_handle_type);
    };

    struct async_connect_t {
        using value_signature = execution::set_value_t();
        endpoint peer;
    };
}