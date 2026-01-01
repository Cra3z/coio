#pragma once
#include "define.h"
#include "router.h"

namespace http {
    auto connection(tcp_socket socket, router& router) -> coio::task<>;
}