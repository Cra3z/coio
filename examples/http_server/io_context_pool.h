#pragma once
#include <cstddef>
#include <thread>
#include <vector>
#include "define.h"

namespace http {
    class io_context_pool {
    public:
        explicit io_context_pool(std::size_t count = std::thread::hardware_concurrency());

        io_context_pool(const io_context_pool&) = delete;

        io_context_pool& operator= (const io_context_pool&) = delete;

        auto stop() -> void {
            for (auto& ctx : io_contexts_) {
                ctx->request_stop();
            }
        }

        auto get_scheduler() noexcept -> io_context::scheduler;

    private:
        std::size_t next_ = 0;
        std::vector<std::unique_ptr<io_context>> io_contexts_;
        std::vector<std::jthread> threads_;
        std::vector<coio::work_guard<io_context>> work_guards_;
    };
}