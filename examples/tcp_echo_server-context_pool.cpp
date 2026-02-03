#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/epoll_context.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/utils/signal_set.h>
#include "common.h"

#if COIO_OS_LINUX
using io_context = coio::epoll_context;
#endif
using tcp_socket = coio::tcp::socket<io_context::scheduler>;
using tcp_acceptor = coio::tcp::acceptor<io_context::scheduler>;

class io_context_pool {
public:
    explicit io_context_pool(std::size_t count) {
        COIO_ASSERT(count > 0);
        io_contexts_.reserve(count);
        work_guards_.reserve(count);
        threads_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            work_guards_.emplace_back(*io_contexts_.emplace_back(std::make_unique<io_context>()));
        }

        for (std::size_t i = 0; i < count; ++i) {
            threads_.emplace_back([this, i] {
                ::debug("worker started");
                io_contexts_[i]->run();
                ::debug("worker finished");
            });
        }
    }

    io_context_pool(const io_context_pool&) = delete;

    io_context_pool& operator= (const io_context_pool&) = delete;

    auto stop() -> void {
        for (auto& ctx : io_contexts_) {
            ctx->request_stop();
        }
        work_guards_.clear();
    }

    auto get_scheduler() noexcept -> io_context::scheduler {
        return io_contexts_[std::exchange(next_, (next_ + 1) % io_contexts_.size())]->get_scheduler();
    }

private:
    std::size_t next_ = 0;
    std::vector<std::unique_ptr<io_context>> io_contexts_;
    std::vector<std::jthread> threads_;
    std::vector<coio::work_guard<io_context>> work_guards_;
};

auto handle_connection(tcp_socket socket) -> coio::task<> {
    auto remote_endpoint = socket.remote_endpoint();
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

auto start_server(io_context_pool& pool) -> coio::task<> {
    tcp_acceptor acceptor{pool.get_scheduler(), coio::endpoint{coio::ipv4_address::any(), 8086}};
    ::debug("server \"{}\" start...", acceptor.local_endpoint());
    coio::async_scope scope;
    try {
        while (true) {
            tcp_socket socket = co_await acceptor.async_accept(pool.get_scheduler());
            scope.spawn(handle_connection(std::move(socket)));
        }
    }
    catch (const std::system_error& e) {
        ::debug("acceptor error: {}", e.what());
    }
    co_await scope.join();
}

auto signal_watchdog() -> coio::task<> {
    coio::signal_set signals{SIGINT, SIGTERM};
    const int signum = co_await signals.async_wait();
    ::debug("server stop with signal: ({}){}", signum, ::strsignal(signum));
    co_await coio::just_stopped();
}

auto main() -> int {
    io_context_pool pool{4};
    coio::this_thread::sync_wait(coio::when_any(
        signal_watchdog(),
        start_server(pool)
    ));
}