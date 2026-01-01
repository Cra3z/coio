#include "connection.h"
#include "io_context_pool.h"
#include "router.h"
#include "../common.h"

auto start_server(http::tcp_acceptor& acceptor, http::io_context_pool& pool) -> coio::task<> {
    http::router router;
    coio::async_scope scope;
    try {
        while (true) {
            scope.spawn(http::connection(co_await acceptor.async_accept(pool.get_scheduler()), router));
        }
    }
    catch (const std::exception& e) {
        ::debug("Acceptor error: {}", e.what());
    }
    co_await scope.join();
}

auto main() -> int try {
    static constexpr std::uint16_t port = 8080;
    http::io_context_pool pool{4};
    http::tcp_acceptor acceptor(pool.get_scheduler());
    acceptor.open(coio::tcp::v6());
    acceptor.set_option(http::tcp_acceptor::reuse_address(true));
    acceptor.set_option(http::tcp_acceptor::v6_only(false));
    acceptor.bind({coio::ipv6_address::any(), port});
    acceptor.listen();
    ::debug("server started at \"http://localhost:{}\"", port);
    coio::sync_wait(start_server(acceptor, pool));
}
catch (const std::invalid_argument& e) {
    ::debug("[FATAL] {}", e.what());
    return EXIT_FAILURE;
}