#pragma once
#include <algorithm>
#include <concepts>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>
#include "config.h"
#include "../utils/atomutex.h"

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

        COIO_ALWAYS_INLINE auto enqueue(Op& op) -> void {
            {
                std::scoped_lock _{op_queue_mtx_};
                this->do_enqueue(op);
            }
            event_.wakeup();
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
        COIO_ALWAYS_INLINE auto do_enqueue(Op& op) -> void {
            if (auto old_tail = std::exchange(op_queue_tail_, &op)) {
                std::invoke(NextAccessor, old_tail) = &op;
            }
            if (op_queue_head_ == nullptr) op_queue_head_ = &op;
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


    template<typename Op, std::regular_invocable<const Op&> auto Proj, typename Allocator = std::allocator<void>>
        requires std::three_way_comparable_with<
            std::chrono::steady_clock::time_point, std::invoke_result_t<decltype(Proj), const Op&>
        >
    class timer_queue {
    private:
        using value_type = std::reference_wrapper<Op>;
        using reference = Op&;
        using allocator_type = std::allocator_traits<Allocator>::template rebind_alloc<value_type>;

    public:
        timer_queue() = default;

        explicit timer_queue(Allocator alloc) noexcept : underlying_(allocator_type(alloc)) {}

        timer_queue(const timer_queue&) = delete;

        ~timer_queue() = default;

        auto operator= (const timer_queue&) -> timer_queue& = delete;

        COIO_ALWAYS_INLINE auto add(reference op) -> bool {
            const auto deadline = std::invoke(Proj, op);
            std::scoped_lock _{mtx_};
            underlying_.emplace_back(op);
            std::ranges::push_heap(underlying_, std::ranges::greater{}, Proj);
            return deadline <= std::invoke(Proj, underlying_.front());
        }

        COIO_ALWAYS_INLINE auto remove(reference op) -> bool {
            std::scoped_lock _{mtx_};
            const bool erased = std::erase_if(underlying_, [&op](reference i) noexcept { return &i == &op; });
            std::ranges::make_heap(underlying_, std::ranges::greater{}, Proj);
            return erased;
        }

        template<typename BaseOp, typename Next> requires std::derived_from<Op, BaseOp>
        COIO_ALWAYS_INLINE auto take_ready_timers(BaseOp*& head, const Next& next) -> void {
            head = nullptr;
            std::scoped_lock _{mtx_};
            if (underlying_.empty()) {
                return;
            }
            auto last = underlying_.end();
            while (underlying_.begin() != last) {
                if (std::invoke(Proj, underlying_.front()) > std::chrono::steady_clock::now()) break;
                std::ranges::pop_heap(underlying_.begin(), last, std::ranges::greater{}, Proj);
                --last;
            }

            for (reference op : std::ranges::subrange{last, underlying_.end()}) {
                std::invoke(next, op) = std::exchange(head, &op);
            }
            underlying_.erase(last, underlying_.end());
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto earliest() noexcept -> std::optional<std::chrono::steady_clock::time_point> {
            std::scoped_lock _{mtx_};
            if (underlying_.empty()) return {};
            return std::invoke(Proj, underlying_.front());
        }

    private:
        std::vector<value_type, allocator_type> underlying_;
        atomutex mtx_;
    };
}
