#pragma once
#include <coio/asyncio/epoll_context.h>
#include <coio/net/tcp.h>

namespace http {
    using io_context = coio::epoll_context;
    using tcp_socket = coio::tcp::socket<io_context::scheduler>;
    using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;
    using tcp_resolver = coio::tcp::resolver<io_context::scheduler>;
}