#pragma once
#include <atomic>
#include "../concepts.h"

namespace coio::detail {
    enum class stack_status : unsigned char {
        empty_and_never_pushed, ///< No objects have ever been pushed into this list
        empty_but_pushed, ///< The list once had elements but all have been popped out,
        not_empty ///< The list has elements
    };

    template<typename T>
    class intrusive_stack {
        static_assert(unqualified_object<T> and not std::is_array_v<T>);
    public:
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = T&;

    public:
        explicit intrusive_stack(pointer (T::* next)) noexcept : head_(this), next_(next) {}

        /// return the list status before pushing
        auto push(reference object) noexcept -> stack_status {
            auto old_top = head_.load(std::memory_order_acquire);
            do {
                object.*next_ = old_top == this ? nullptr : static_cast<pointer>(old_top);
            }
            while (not head_.compare_exchange_weak(
                old_top,
                &object,
                std::memory_order_acq_rel
            ));

            if (old_top == this) return stack_status::empty_and_never_pushed;
            if (old_top == nullptr) return stack_status::empty_but_pushed;
            return stack_status::not_empty;
        }

        [[nodiscard]]
        auto pop_all() noexcept -> pointer {
            auto head = head_.exchange(nullptr, std::memory_order_acq_rel);
            return head == this ? nullptr : static_cast<pointer>(head);
        }

        [[nodiscard]]
        auto status() const noexcept -> stack_status {
            auto head = head_.load(std::memory_order_acquire);
            if (head == this) return stack_status::empty_and_never_pushed;
            if (head == nullptr) return stack_status::empty_but_pushed;
            return stack_status::not_empty;
        }

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return status() != stack_status::not_empty;
        }

    private:
        std::atomic<void*> head_;
        pointer (T::* next_);
    };
}