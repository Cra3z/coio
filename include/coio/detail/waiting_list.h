#pragma once
#include <atomic>
#include "../concepts.h"

namespace coio::detail {
    template<typename Awaiter>
    class waiting_list {
        static_assert(awaiter<Awaiter>);
        static_assert(not std::movable<Awaiter>);
    public:
        using value_type = Awaiter*;

    public:
        explicit waiting_list(Awaiter* (Awaiter::* next)) noexcept : next_(next) {}

        auto push(Awaiter& awt) -> void {
            awt.*next_ = head_.exchange(&awt, std::memory_order_acq_rel);
        }

        [[nodiscard]]
        auto pop() -> value_type {
            auto node = head_.load(std::memory_order::acquire);
            do {
                if (node == nullptr) return nullptr;
            }
            while (not head_.compare_exchange_weak(
                node,
                node->*next_,
                std::memory_order_acq_rel
            ));
            return node;
        }

        [[nodiscard]]
        auto pop_all() -> value_type {
            return head_.exchange(nullptr, std::memory_order_acq_rel);
        }

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return head_.load(std::memory_order::acquire) == nullptr;
        }

    private:
        std::atomic<Awaiter*> head_{nullptr};
        Awaiter* (Awaiter::* next_);
    };
}