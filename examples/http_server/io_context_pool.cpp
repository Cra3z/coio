#include "io_context_pool.h"

namespace http {
    io_context_pool::io_context_pool(std::size_t count) {
        COIO_ASSERT(count > 0);
        io_contexts_.reserve(count);
        work_guards_.reserve(count);
        threads_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            work_guards_.emplace_back(*io_contexts_.emplace_back(std::make_unique<io_context>()));
            threads_.emplace_back([this, i] {
                io_contexts_[i]->run();
            });
        }
    }

    auto io_context_pool::get_scheduler() noexcept -> io_context::scheduler {
        return io_contexts_[std::exchange(next_, (next_ + 1) % io_contexts_.size())]->get_scheduler();
    }
}
