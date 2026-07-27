#pragma once
#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>
#include <coio/detail/config.h>
#include <coio/detail/intrusive_list.h>
#include <coio/utils/atomutex.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio::detail {
    // Intrusive MPSC queue
    template<typename Op, auto NextAccessor> requires std::is_nothrow_invocable_r_v<Op*, decltype(NextAccessor), Op*>
    class op_queue {
    public:
        op_queue() = default;

        op_queue(const op_queue&) = delete;

        ~op_queue() = default;

        auto operator= (const op_queue&) -> op_queue& = delete;

        COIO_ALWAYS_INLINE auto enqueue(Op& op) -> void {
            this->publish(this->reverse(&op), &op);
        }

        template<typename Ops> requires
            std::ranges::input_range<Ops> and
            std::convertible_to<std::ranges::range_reference_t<Ops>, Op&>
        COIO_ALWAYS_INLINE auto bulk_enqueue(Ops&& ops) -> bool {
            Op* head = nullptr;
            Op* tail = nullptr;

            for (Op& op : ops) {
                COIO_ASSERT(std::invoke(NextAccessor, &op) == nullptr);
                if (tail == nullptr) tail = &op;
                std::invoke(NextAccessor, &op) = head;
                head = &op;
            }

            if (head == nullptr) return false;
            this->publish(head, tail);
            return true;
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto dequeue() -> Op* {
            if (front_ == nullptr) {
                Op* incoming = incoming_.exchange(nullptr, std::memory_order_acquire);
                if (incoming == nullptr) return nullptr;
                front_ = this->reverse(incoming);
            }

            Op* op = front_;
            front_ = std::invoke(NextAccessor, op);
            std::invoke(NextAccessor, op) = nullptr;
            return op;
        }

    private:
        [[nodiscard]]
        COIO_ALWAYS_INLINE static auto reverse(Op* node) noexcept -> Op* {
            Op* reversed = nullptr;

            while (true) {
                Op* next = std::invoke(NextAccessor, node);
                std::invoke(NextAccessor, node) = reversed;
                reversed = node;
                if (next == nullptr) break;
                node = next;
            }

            return reversed;
        }

        COIO_ALWAYS_INLINE auto publish(Op* head, Op* tail) noexcept -> void {
            COIO_ASSERT(head != nullptr and tail != nullptr);
            Op* old = incoming_.load(std::memory_order_relaxed);
            do {
                std::invoke(NextAccessor, tail) = old;
            }
            while (not incoming_.compare_exchange_weak(
                old,
                head,
                std::memory_order_release,
                std::memory_order_relaxed
            ));
        }

    private:
        alignas(std::hardware_destructive_interference_size) std::atomic<Op*> incoming_{};
        alignas(std::hardware_destructive_interference_size) Op* front_ = nullptr;
    };


    template<typename Op, std::regular_invocable<const Op&> auto Proj, auto HeapIndexAccessor, typename Allocator = std::allocator<void>>
        requires std::three_way_comparable_with<
            std::chrono::steady_clock::time_point, std::invoke_result_t<decltype(Proj), const Op&>
        > and std::is_nothrow_invocable_r_v<std::size_t&, decltype(HeapIndexAccessor), Op&>
    class timer_queue {
    private:
        using reference = Op&;

        struct item {
            std::chrono::steady_clock::time_point deadline;
            Op* op;
        };

        using allocator_type = std::allocator_traits<Allocator>::template rebind_alloc<item>;

        static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    public:
        timer_queue() = default;

        explicit timer_queue(Allocator alloc) noexcept : underlying_(allocator_type(alloc)) {}

        timer_queue(const timer_queue&) = delete;

        ~timer_queue() = default;

        auto operator= (const timer_queue&) -> timer_queue& = delete;

        COIO_ALWAYS_INLINE auto add(reference op) -> bool {
            const auto deadline = std::invoke(Proj, op);
            std::scoped_lock _{mtx_};
            const auto index = underlying_.size();
            underlying_.emplace_back(deadline, &op);
            std::invoke(HeapIndexAccessor, op) = index;
            up_node(index);
            return std::invoke(HeapIndexAccessor, op) == 0;
        }

        COIO_ALWAYS_INLINE auto remove(reference op) -> bool {
            std::scoped_lock _{mtx_};
            const auto index = std::invoke(HeapIndexAccessor, op);
            if (index >= underlying_.size()) return false;
            do_remove(index);
            return true;
        }

        template<typename BaseOp> requires std::derived_from<Op, BaseOp>
        COIO_ALWAYS_INLINE auto take_ready_timers(intrusive_list<BaseOp>& list) -> void {
            std::scoped_lock _{mtx_};
            while (not underlying_.empty() and std::chrono::steady_clock::now() >= underlying_.front().deadline) {
                Op* op = underlying_.front().op;
                COIO_ASSERT(op != nullptr);
                do_remove(0);
                list.push_back(*op);
            }
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto earliest() noexcept -> std::optional<std::chrono::steady_clock::time_point> {
            std::scoped_lock _{mtx_};
            if (underlying_.empty()) return {};
            return underlying_.front().deadline;
        }

    private:
        COIO_ALWAYS_INLINE auto up_node(std::size_t index) noexcept -> void {
            while (index > 0) {
                const auto parent = (index - 1) / 2;
                if (underlying_[index].deadline >= underlying_[parent].deadline) break;
                swap_node(index, parent);
                index = parent;
            }
        }

        COIO_ALWAYS_INLINE auto down_node(std::size_t index) noexcept -> void {
            auto left_child = index * 2 + 1;
            while (left_child < underlying_.size()) {
                auto next = left_child;
                const auto right_child = left_child + 1;
                if (right_child < underlying_.size() and underlying_[right_child].deadline < underlying_[left_child].deadline) {
                    next = right_child;
                }
                if (underlying_[index].deadline < underlying_[next].deadline) break;
                swap_node(index, next);
                index = next;
                left_child = index * 2 + 1;
            }
        }

        COIO_ALWAYS_INLINE auto swap_node(std::size_t i, std::size_t j) noexcept -> void {
            std::swap(underlying_[i], underlying_[j]);
            std::invoke(HeapIndexAccessor, *underlying_[i].op) = i;
            std::invoke(HeapIndexAccessor, *underlying_[j].op) = j;
        }

        COIO_ALWAYS_INLINE auto do_remove(std::size_t index) noexcept -> void {
            if (index == underlying_.size() - 1) {
                std::invoke(HeapIndexAccessor, *underlying_[index].op) = npos;
                underlying_.pop_back();
            }
            else {
                swap_node(index, underlying_.size() - 1);
                std::invoke(HeapIndexAccessor, *underlying_.back().op) = npos;
                underlying_.pop_back();
                if (index > 0 and underlying_[index].deadline < underlying_[(index - 1) / 2].deadline) {
                    up_node(index);
                }
                else {
                    down_node(index);
                }
            }
        }

    private:
        std::vector<item, allocator_type> underlying_;
        atomutex mtx_;
    };
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
