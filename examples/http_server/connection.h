#pragma once
#include "define.h"
#include "router.h"

namespace http {
    auto connection(
        tcp_socket socket,
        coio::endpoint remote_endpoint,
        router& router,
        coio::inplace_stop_token stop_token
    ) -> coio::task<>;
}