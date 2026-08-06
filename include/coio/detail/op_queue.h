#pragma once
#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <mutex>
#include <new>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <coio/detail/config.h>
#include <coio/detail/intrusive_list.h>
#include <coio/utils/atomutex.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio::detail {
    COIO_MSVC_SUPPRESS_PUSH()
    COIO_MSVC_IGNORE(4324) // ignore C4324: structure was padded due to alignment specifier
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
        alignas(64) std::atomic<Op*> incoming_{};
        alignas(64) Op* front_ = nullptr;
    };
    COIO_MSVC_SUPPRESS_POP()

    template<typename Op>
    struct timer_heap_links {
        // `prev` is the parent when the node is a first child, the previous sibling otherwise;
        // null when the node is the root or not linked into the heap
        Op* prev = nullptr;
        Op* child = nullptr;
        Op* sibling = nullptr;
    };

    // intrusive pairing heap: no allocation, every operation is noexcept;
    // `add` is O(1), `remove`/`take_ready_timers` pop in amortized O(log n)
    template<typename Op, std::regular_invocable<const Op&> auto Proj, auto LinksAccessor>
        requires std::three_way_comparable_with<
            std::chrono::steady_clock::time_point, std::invoke_result_t<decltype(Proj), const Op&>
        > and std::is_nothrow_invocable_r_v<timer_heap_links<Op>&, decltype(LinksAccessor), Op&>
    class timer_queue {
    private:
        using reference = Op&;

    public:
        timer_queue() = default;

        timer_queue(const timer_queue&) = delete;

        ~timer_queue() = default;

        auto operator= (const timer_queue&) -> timer_queue& = delete;

        COIO_ALWAYS_INLINE auto add(reference op) noexcept -> bool {
            std::scoped_lock _{mtx_};
            links(op) = {};
            root_ = root_ == nullptr ? &op : meld(root_, &op);
            return root_ == &op;
        }

        COIO_ALWAYS_INLINE auto remove(reference op) noexcept -> bool {
            std::scoped_lock _{mtx_};
            if (&op == root_) {
                static_cast<void>(pop_root());
                return true;
            }
            if (links(op).prev == nullptr) return false; // never added, or already fired/removed
            unlink(op);
            if (Op* subtree = merge_children(links(op).child)) {
                root_ = meld(root_, subtree);
            }
            links(op) = {};
            return true;
        }

        template<typename BaseOp> requires std::derived_from<Op, BaseOp>
        COIO_ALWAYS_INLINE auto take_ready_timers(intrusive_list<BaseOp>& list) noexcept -> void {
            std::scoped_lock _{mtx_};
            const auto now = std::chrono::steady_clock::now();
            while (root_ != nullptr and now >= std::invoke(Proj, *root_)) {
                list.push_back(*pop_root());
            }
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto earliest() noexcept -> std::optional<std::chrono::steady_clock::time_point> {
            std::scoped_lock _{mtx_};
            if (root_ == nullptr) return {};
            return std::invoke(Proj, *root_);
        }

    private:
        COIO_ALWAYS_INLINE static auto links(reference op) noexcept -> timer_heap_links<Op>& {
            return std::invoke(LinksAccessor, op);
        }

        // pre: `a` and `b` are both roots of well-formed trees (null `prev`/`sibling`)
        COIO_ALWAYS_INLINE static auto meld(Op* a, Op* b) noexcept -> Op* {
            if (std::invoke(Proj, *b) < std::invoke(Proj, *a)) std::swap(a, b);
            // `b` becomes the first child of `a`
            links(*b).prev = a;
            links(*b).sibling = links(*a).child;
            if (links(*a).child != nullptr) links(*links(*a).child).prev = b;
            links(*a).child = b;
            return a;
        }

        // two-pass pairwise merge of a child list; returns the resulting root (or null)
        static auto merge_children(Op* first) noexcept -> Op* {
            if (first == nullptr) return nullptr;

            Op* merged = nullptr; // merged pairs, stacked via `sibling`
            while (first != nullptr) {
                Op* a = first;
                Op* b = links(*a).sibling;
                first = b == nullptr ? nullptr : links(*b).sibling;
                links(*a).prev = nullptr;
                links(*a).sibling = nullptr;
                if (b != nullptr) {
                    links(*b).prev = nullptr;
                    links(*b).sibling = nullptr;
                    a = meld(a, b);
                }
                links(*a).sibling = merged;
                merged = a;
            }

            Op* result = merged;
            merged = links(*result).sibling;
            links(*result).sibling = nullptr;
            while (merged != nullptr) {
                Op* next = links(*merged).sibling;
                links(*merged).sibling = nullptr;
                result = meld(result, merged);
                merged = next;
            }
            return result;
        }

        COIO_ALWAYS_INLINE auto pop_root() noexcept -> Op* {
            Op* top = root_;
            root_ = merge_children(links(*top).child);
            links(*top) = {};
            return top;
        }

        // pre: `op` is linked and is not the root
        COIO_ALWAYS_INLINE static auto unlink(reference op) noexcept -> void {
            Op* prev = links(op).prev;
            Op* sibling = links(op).sibling;
            if (links(*prev).child == &op) links(*prev).child = sibling; // `prev` is the parent
            else links(*prev).sibling = sibling;                        // `prev` is the previous sibling
            if (sibling != nullptr) links(*sibling).prev = prev;
        }

    private:
        Op* root_ = nullptr;
        atomutex mtx_;
    };
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
