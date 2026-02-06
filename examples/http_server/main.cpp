#include <filesystem>
#include <coio/utils/signal_set.h>
#include "connection.h"
#include "io_context_pool.h"
#include "router.h"
#include "../common.h"

// Set this to the source directory containing static files
#ifndef HTTP_SERVER_STATIC_DIR
#define HTTP_SERVER_STATIC_DIR "static"
#endif

constexpr std::uint16_t port = 8080;

auto signal_watchdog(http::io_context_pool& pool) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::println("server stop with signal: ({}){}", signum, ::strsignal(signum));
    pool.stop();
}

auto start_server(http::io_context_pool& pool, coio::async_scope& scope, http::router& router) -> coio::task<> try {
    http::tcp_acceptor acceptor(pool.get_scheduler());
    acceptor.open(coio::tcp::v6());
    acceptor.set_option(http::tcp_acceptor::reuse_address(true));
    acceptor.set_option(http::tcp_acceptor::v6_only(false));
    acceptor.bind({coio::ipv6_address::any(), port});
    acceptor.listen();
    ::debug("server started at http://localhost:{}", port);
    while (true) {
        http::tcp_socket socket = co_await acceptor.async_accept(pool.get_scheduler());
        auto endpoint = socket.remote_endpoint();
        scope.spawn(http::connection(
            std::move(socket),
            endpoint,
            router
        ));
    }
}
catch (const std::exception& e) {
    ::debug("acceptor error: {}", e.what());
}

auto main() -> int try {
    http::router router{HTTP_SERVER_STATIC_DIR};
    http::io_context_pool pool{4};
    coio::async_scope scope;
    scope.spawn(signal_watchdog(pool));
    scope.spawn(start_server(pool, scope, router));
    coio::this_thread::sync_wait(scope.join());
}
catch (const std::exception& e) {
    ::debug("[FATAL] {}", e.what());
    return EXIT_FAILURE;
}