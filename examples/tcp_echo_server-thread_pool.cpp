#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/utils/signal_set.h>
#include "common.h"

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
using io_context = coio::epoll_context;
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
using io_context = coio::iocp_context;
#endif

using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

class thread_pool {
public:
    explicit thread_pool(std::size_t thread_count) {
        work_guards_.reserve(thread_count);
        threads_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            work_guards_.emplace_back(context_);
        }
        for (std::size_t i = 0; i < thread_count; ++i) {
            threads_.emplace_back([this] {
                ::debug("worker started");
                context_.run();
                ::debug("worker finished");
            });
        }
    }

    ~thread_pool() {
        stop();
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    auto stop() -> void {
        context_.request_stop();
        work_guards_.clear();
    }

    auto get_scheduler() noexcept -> io_context::scheduler {
        return context_.get_scheduler();
    }

private:
    io_context context_;
    std::vector<std::thread> threads_;
    std::vector<coio::work_guard<io_context>> work_guards_;
};

auto handle_connection(tcp_socket socket) -> coio::task<> {
    auto remote_endpoint = socket.remote_endpoint();
    ::debug("new connection from [{}]", remote_endpoint);
    try {
        char buffer[1024];
        while (true) {
            const auto length = co_await socket.async_read_some(coio::as_writable_bytes(buffer));
            co_await coio::async_write(socket, coio::as_bytes(buffer, length));
        }
    }
    catch (const std::system_error& e) {
        ::debug("connection with [{}] broken because \"{}\"", remote_endpoint, e.what());
    }
}

auto start_server(io_context::scheduler sched, coio::async_scope& scope) -> coio::task<> try {
    tcp_acceptor acceptor{sched, coio::endpoint{coio::ipv4_address::any(), 8086}};
    ::debug("server \"{}\" start...", acceptor.local_endpoint());
    while (true) {
        scope.spawn(handle_connection(co_await acceptor.async_accept()));
    }
}
catch (const std::system_error& e) {
    ::debug("acceptor error: {}", e.what());
}

auto signal_watchdog(thread_pool& pool) -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::debug("server stop with signal: ({}){}", signum, coio::strsignal(signum));
    pool.stop();
}

auto main() -> int {
    using namespace std::chrono_literals;
    thread_pool pool{4};
    coio::async_scope scope;
    scope.spawn(signal_watchdog(pool));
    scope.spawn(start_server(pool.get_scheduler(), scope));
    coio::this_thread::sync_wait(scope.join());
}