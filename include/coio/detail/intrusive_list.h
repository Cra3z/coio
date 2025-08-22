#pragma once
#include <atomic>
#include "../concepts.h"

namespace coio::detail {
    template<typename T>
    class intrusive_list {
        static_assert(unqualified_object<T> and not std::is_array_v<T>);
    public:
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = T&;

    public:
        explicit intrusive_list(pointer (T::* next)) noexcept : next_(next) {}

        auto push(reference object) noexcept -> void {
            object.*next_ = head_.exchange(&object, std::memory_order_acq_rel);
        }

        [[nodiscard]]
        auto pop() noexcept -> pointer {
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
        auto pop_all() noexcept -> pointer {
            return head_.exchange(nullptr, std::memory_order_acq_rel);
        }

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return head_.load(std::memory_order::acquire) == nullptr;
        }

    private:
        std::atomic<pointer> head_{nullptr};
        pointer (T::* next_);
    };
}