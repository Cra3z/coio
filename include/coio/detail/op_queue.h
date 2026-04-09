#pragma once
#include <chrono>
#include <concepts>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>
#include <coio/detail/config.h>
#include <coio/utils/atomutex.h>

namespace coio::detail {
    class queue_event {
    public:
        enum class status : unsigned char {
            available,
            try_again,
            stop_requested
        };
        using enum status;

    public:
        auto wakeup(std::ptrdiff_t update = 1) noexcept -> void {
            COIO_ASSERT(update >= 0);
            if (update == 0) [[unlikely]] return;

            auto old = count_.load(std::memory_order_relaxed);
            while (true) {
                if (old == sentinel) return;
                COIO_ASSERT(update <= max() - old);
                if (count_.compare_exchange_weak(old, old + update, std::memory_order_release, std::memory_order_relaxed)) {
                    const auto waiting_upper_bound = waiting_.load();
                    if (waiting_upper_bound == 0) {}
                    else if (waiting_upper_bound <= update) {
                        count_.notify_all();
                    }
                    else {
                        for (std::ptrdiff_t i = 0; i < update; ++i) count_.notify_one();
                    }
                    return;
                }
            }
        }

        [[nodiscard]]
        auto wait() noexcept -> status {
            auto old = count_.load(std::memory_order_acquire);

            while (true) {
                if (old == sentinel) {
                    return status::stop_requested;
                }

                while (old == 0) {
                    waiting_.fetch_add(1);
                    count_.wait(0, std::memory_order_acquire);
                    waiting_.fetch_sub(1);

                    old = count_.load(std::memory_order_acquire);
                    if (old == sentinel) {
                        return status::stop_requested;
                    }
                }

                if (count_.compare_exchange_weak(old, old - 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                    return status::available;
                }
            }
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto try_wait() noexcept -> status {
            auto old = count_.load(std::memory_order_acquire);

            while (true) {
                if (old == sentinel) return status::stop_requested;
                if (old == 0) return status::try_again;

                if (count_.compare_exchange_weak(old, old - 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                    return status::available;
                }
            }
        }

        COIO_ALWAYS_INLINE auto request_stop() noexcept -> void {
            auto old = count_.load(std::memory_order_acquire);
            while (old != sentinel) {
                if (count_.compare_exchange_weak(old, sentinel, std::memory_order_release, std::memory_order_acquire)) {
                    count_.notify_all();
                    return;
                }
            }
        }

        [[nodiscard]]
        static constexpr auto max() noexcept -> std::ptrdiff_t {
            return std::numeric_limits<std::ptrdiff_t>::max();
        }

    private:
        std::atomic<std::ptrdiff_t> count_{0};
        std::atomic<std::ptrdiff_t> waiting_{0};
        static constexpr std::ptrdiff_t sentinel = -1;
    };


    template<typename Op, auto NextAccessor> requires std::is_nothrow_invocable_r_v<Op*, decltype(NextAccessor), Op*>
    class op_queue {
    public:
        op_queue() = default;

        op_queue(const op_queue&) = delete;

        ~op_queue() = default;

        auto operator= (const op_queue&) -> op_queue& = delete;

        COIO_ALWAYS_INLINE auto enqueue(Op& op) -> std::size_t {
            std::size_t count = 0;
            {
                std::scoped_lock _{op_queue_mtx_};
                count = this->do_enqueue(op);
            }
            event_.wakeup(static_cast<std::ptrdiff_t>(count));
            return count;
        }

        template<typename Ops> requires
            std::ranges::input_range<Ops> and
            std::convertible_to<std::ranges::range_reference_t<Ops>, Op&>
        COIO_ALWAYS_INLINE auto bulk_enqueue(Ops&& ops) -> std::size_t {
            std::size_t count = 0;
            {
                std::scoped_lock _{op_queue_mtx_};
                count = this->do_bulk_enqueue(std::forward<Ops>(ops));
            }
            event_.wakeup(static_cast<std::ptrdiff_t>(count));
            return count;
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto try_dequeue() -> Op* {
            if (event_.try_wait() == queue_event::try_again) return nullptr;
            std::scoped_lock _{op_queue_mtx_};
            return do_dequeue();
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto dequeue() -> Op* {
            // wait until work may be available, or stop is requested.
            // The actual queue state is checked under the mutex below.
            static_cast<void>(event_.wait());
            std::scoped_lock _{op_queue_mtx_};
            return do_dequeue();
        }

        COIO_ALWAYS_INLINE auto request_stop() noexcept -> void {
            event_.request_stop();
        }

    private:
        COIO_ALWAYS_INLINE auto do_enqueue(Op& op) -> std::size_t {
            if (auto old_tail = std::exchange(op_queue_tail_, &op)) {
                std::invoke(NextAccessor, old_tail) = &op;
            }
            std::size_t count = 1;
            while (auto tail_next = std::invoke(NextAccessor, op_queue_tail_)) {
                op_queue_tail_ = tail_next;
                ++count;
            }
            if (op_queue_head_ == nullptr) op_queue_head_ = &op;
            return count;
        }

        template<typename Ops> requires
            std::ranges::input_range<Ops> and
            std::convertible_to<std::ranges::range_reference_t<Ops>, Op&>
        COIO_ALWAYS_INLINE auto do_bulk_enqueue(Ops&& ops) -> std::size_t {
            std::size_t count = 0;
            for (Op& op : ops) {
                this->do_enqueue(op);
                ++count;
            }
            return count;
        }

        COIO_ALWAYS_INLINE auto do_dequeue() noexcept -> Op* {
            if (op_queue_head_ == nullptr) return nullptr;
            if (op_queue_head_ == op_queue_tail_) op_queue_tail_ = nullptr;
            return std::exchange(op_queue_head_, std::invoke(NextAccessor, op_queue_head_));
        }

    private:
        atomutex op_queue_mtx_;
        Op* op_queue_head_{};
        Op* op_queue_tail_{};
        queue_event event_;
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

        template<typename BaseOp, typename Next> requires std::derived_from<Op, BaseOp>
        COIO_ALWAYS_INLINE auto take_ready_timers(BaseOp*& head, const Next& next) -> void {
            head = nullptr;
            std::scoped_lock _{mtx_};
            while (not underlying_.empty() and std::chrono::steady_clock::now() >= underlying_.front().deadline) {
                Op* op = underlying_.front().op;
                do_remove(0);
                std::invoke(next, *op) = std::exchange(head, op);
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
