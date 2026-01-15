#include <filesystem>
#include <coio/utils/signal_set.h>
#include "connection.h"
#include "io_context_pool.h"
#include "router.h"
#include "../common.h"

// Set this to the source directory containing static files
#ifndef HTTP_SERVER_STATIC_DIR
#define HTTP_SERVER_STATIC_DIR "."
#endif

auto signal_watchdog(http::io_context_pool& pool) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::println("server stop with signal: ({}){}", signum, ::strsignal(signum));
    pool.stop();
}

auto start_server(http::tcp_acceptor& acceptor, http::io_context_pool& pool, const std::filesystem::path& static_dir) -> coio::task<> {
    http::router router(static_dir);
    ::debug("Static files directory: {}", static_dir.string());
    coio::async_scope scope;
    scope.spawn(signal_watchdog(pool));
    try {
        while (true) {
            auto sched = pool.get_scheduler();
            auto stop_token = sched.context().get_stop_token();
            http::tcp_socket socket = co_await acceptor.async_accept(sched, stop_token);
            scope.spawn(http::connection(
                std::move(socket),
                socket.remote_endpoint(),
                router,
                stop_token
            ));
        }
    }
    catch (const std::exception& e) {
        ::debug("Acceptor error: {}", e.what());
    }
    co_await scope.join();
}

auto main() -> int try {
    static constexpr std::uint16_t port = 8080;

    // Try to find static directory
    std::filesystem::path static_dir = HTTP_SERVER_STATIC_DIR;
    static_dir /= "static";

    // If not found, try current directory
    if (!std::filesystem::exists(static_dir)) {
        static_dir = std::filesystem::current_path() / "static";
    }

    http::io_context_pool pool{4};
    http::tcp_acceptor acceptor(pool.get_scheduler());
    acceptor.open(coio::tcp::v6());
    acceptor.set_option(http::tcp_acceptor::reuse_address(true));
    acceptor.set_option(http::tcp_acceptor::v6_only(false));
    acceptor.bind({coio::ipv6_address::any(), port});
    acceptor.listen();
    ::debug("server started at http://localhost:{}", port);
    coio::sync_wait(start_server(acceptor, pool, static_dir));
}
catch (const std::invalid_argument& e) {
    ::debug("[FATAL] {}", e.what());
    return EXIT_FAILURE;
}