#pragma once
#include "../net/basic.h"

namespace coio::detail {
    struct async_read_some_t {
        static auto operation_name() noexcept -> const char* {
            return "async_read_some";
        }

        using result_type = std::size_t;
        std::span<std::byte> buffer;
    };

    struct async_write_some_t {
        static auto operation_name() noexcept -> const char* {
            return "async_write_some";
        }

        using result_type = std::size_t;
        std::span<const std::byte> buffer;
    };

    struct async_receive_t {
        static auto operation_name() noexcept -> const char* {
            return "async_receive";
        }

        using result_type = std::size_t;
        std::span<std::byte> buffer;
    };

    struct async_send_t {
        static auto operation_name() noexcept -> const char* {
            return "async_send";
        }

        using result_type = std::size_t;
        std::span<const std::byte> buffer;
    };

    struct async_receive_from_t {
        static auto operation_name() noexcept -> const char* {
            return "async_receive_from";
        }

        using result_type = std::size_t;
        std::span<std::byte> buffer;
        endpoint peer;
    };

    struct async_send_to_t {
        static auto operation_name() noexcept -> const char* {
            return "async_send_to";
        }

        using result_type = std::size_t;
        std::span<const std::byte> buffer;
        endpoint peer;
    };

    struct async_accept_t {
        static auto operation_name() noexcept -> const char* {
            return "async_accept";
        }

        using result_type = socket_native_handle_type;
    };

    struct async_connect_t {
        static auto operation_name() noexcept -> const char* {
            return "async_connect";
        }

        using result_type = void;
        endpoint peer;
    };
}