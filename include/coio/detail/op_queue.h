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

namespace coio::detail {
    template<typename Op, auto NextAccessor> requires std::is_nothrow_invocable_r_v<Op*, decltype(NextAccessor), Op*>
    class op_queue {
    public:
        op_queue() = default;

        op_queue(const op_queue&) = delete;

        ~op_queue() = default;

        auto operator= (const op_queue&) -> op_queue& = delete;

        auto enqueue(Op& op) -> void {
            std::scoped_lock _{op_queue_mtx_};
            this->unsynchronized_enqueue(op);
        }

        auto unsynchronized_enqueue(Op& op) -> void {
            if (auto old_tail = std::exchange(op_queue_tail_, &op)) {
                std::invoke(NextAccessor, old_tail) = &op;
            }
            if (op_queue_head_ == nullptr) op_queue_head_ = &op;
        }

        template<typename Ops> requires
            std::ranges::input_range<Ops> and
            std::ranges::borrowed_range<Ops> and
            std::convertible_to<std::ranges::range_reference_t<Ops>, Op&>
        auto bulk_enqueue(Ops&& ops) -> void {
            std::scoped_lock _{op_queue_mtx_};
            for (Op& op : ops) {
                this->unsynchronized_enqueue(op);
            }
        }

        [[nodiscard]]
        auto try_dequeue() -> Op* {
            std::scoped_lock _{op_queue_mtx_};
            if (op_queue_head_ == nullptr) return nullptr;
            if (op_queue_head_ == op_queue_tail_) op_queue_tail_ = nullptr;
            return std::exchange(op_queue_head_, std::invoke(NextAccessor, op_queue_head_));
        }

        auto splice(op_queue&& other) -> void {
            COIO_ASSERT(&other != this);
            std::scoped_lock _{op_queue_mtx_, other.op_queue_mtx_};
            while (other.op_queue_head_) {
                if (other.op_queue_head_ == other.op_queue_tail_) other.op_queue_tail_ = nullptr;
                const auto op = std::exchange(other.op_queue_head_, std::invoke(NextAccessor, other.op_queue_head_));
                COIO_ASSERT(op != nullptr);
                if (auto old_tail = std::exchange(op_queue_tail_, op)) {
                    std::invoke(NextAccessor, old_tail) = op;
                }
                if (op_queue_head_ == nullptr) op_queue_head_ = op;
            }
        }

    private:
        std::mutex op_queue_mtx_;
        Op* op_queue_head_{};
        Op* op_queue_tail_{};
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

        auto add(reference op) -> void {
            std::scoped_lock _{mtx_};
            underlying_.emplace_back(op);
            std::ranges::push_heap(underlying_, std::ranges::greater{}, Proj);
        }

        auto remove(reference op) -> bool {
            std::scoped_lock _{mtx_};
            const bool erased = std::erase(underlying_, std::ref(op));
            std::ranges::make_heap(underlying_, std::ranges::greater{}, Proj);
            return erased;
        }

        template<typename BaseOp, auto NextAccessor> requires std::derived_from<Op, BaseOp>
        auto take_ready_timers(op_queue<BaseOp, NextAccessor>& out) -> void {
            std::scoped_lock _{mtx_};
            if (underlying_.empty()) return;
            auto last = underlying_.end();
            while (underlying_.begin() != last) {
                if (std::invoke(Proj, underlying_.front()) > std::chrono::steady_clock::now()) break;
                std::ranges::pop_heap(underlying_.begin(), last, std::ranges::greater{}, Proj);
                --last;
            }
            out.bulk_enqueue(std::ranges::subrange{last, underlying_.end()} | std::views::reverse);
            underlying_.erase(last, underlying_.end());
        }

        [[nodiscard]]
        auto earliest() noexcept -> std::optional<std::chrono::steady_clock::time_point> {
            std::scoped_lock _{mtx_};
            if (underlying_.empty()) return {};
            return std::invoke(Proj, underlying_.front());
        }

    private:
        std::mutex mtx_;
        std::vector<value_type, allocator_type> underlying_;
    };
}