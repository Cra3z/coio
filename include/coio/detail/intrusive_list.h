#pragma once
#include <type_traits>
#include <utility>
#include <coio/detail/concepts.h>

namespace coio::detail {
    template<typename T>
    class intrusive_list {
        static_assert(unqualified_object<T> and not std::is_array_v<T>);

    public:
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;

    public:
        explicit intrusive_list(pointer T::* next) noexcept : next_(next) {}

        intrusive_list(const intrusive_list&) = delete;

        auto operator= (const intrusive_list&) -> intrusive_list& = delete;

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return head_ == nullptr;
        }

        [[nodiscard]]
        auto front() noexcept -> pointer {
            return head_;
        }

        [[nodiscard]]
        auto front() const noexcept -> const_pointer {
            return head_;
        }

        [[nodiscard]]
        auto back() noexcept -> pointer {
            return tail_;
        }

        [[nodiscard]]
        auto back() const noexcept -> const_pointer {
            return tail_;
        }

        auto push_back(reference object) noexcept -> void {
            object.*next_ = nullptr;
            if (tail_) tail_->*next_ = &object;
            else head_ = &object;
            tail_ = &object;
        }

        auto append(reference object) noexcept -> std::size_t {
            pointer old_tail = tail_;
            if (old_tail) old_tail->*next_ = &object;
            else head_ = &object;

            std::size_t count = 1;
            tail_ = &object;
            while (tail_->*next_) {
                tail_ = tail_->*next_;
                ++count;
            }
            return count;
        }

        [[nodiscard]]
        auto pop_front() noexcept -> pointer {
            if (head_ == nullptr) return nullptr;
            pointer object = head_;
            head_ = object->*next_;
            if (head_ == nullptr) tail_ = nullptr;
            object->*next_ = nullptr;
            return object;
        }

        [[nodiscard]]
        auto release() noexcept -> pointer {
            tail_ = nullptr;
            return std::exchange(head_, nullptr);
        }

        auto clear() noexcept -> void {
            head_ = nullptr;
            tail_ = nullptr;
        }

    private:
        pointer head_ = nullptr;
        pointer tail_ = nullptr;
        pointer T::* next_;
    };

}
