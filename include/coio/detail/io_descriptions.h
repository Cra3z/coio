#pragma once
#include <coio/detail/execution.h>
#include <coio/net/basic.h>

namespace coio::detail {
    struct read_some_tag {
        using value_signature = execution::set_value_t(std::size_t);
    };

    struct write_some_tag {
        using value_signature = execution::set_value_t(std::size_t);
    };

    struct read_some_at_tag {
        using value_signature = execution::set_value_t(std::size_t);
    };

    struct write_some_at_tag {
        using value_signature = execution::set_value_t(std::size_t);
    };

    struct receive_tag {
        using value_signature = execution::set_value_t(std::size_t);
    };

    struct send_tag {
        using value_signature = execution::set_value_t(std::size_t);
    };

    struct receive_from_tag {
        using value_signature = execution::set_value_t(endpoint, std::size_t);
    };

    struct send_to_tag {
        using value_signature = execution::set_value_t(std::size_t);
    };

    struct accept_tag {
        using value_signature = execution::set_value_t(socket_native_handle_type);
    };

    struct connect_tag {
        using value_signature = execution::set_value_t();
    };
}
