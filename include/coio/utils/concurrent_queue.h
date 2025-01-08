#pragma once
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <type_traits>
#include <vector>

namespace coio {
    template <typename T, typename Container = std::deque<T>>
    class blocking_queue {
    public:
        using value_type = T;
        using container_type = Container;
    public:
        blocking_queue() = default;

        explicit blocking_queue(std::size_t max_size) noexcept : max_size_(max_size) {}

        blocking_queue(const blocking_queue&) = delete;

        auto operator= (const blocking_queue&) -> blocking_queue& = delete;

        auto push(const value_type& value) ->void {
            emplace(value);
        }

        auto push(value_type&& value) ->void {
            emplace(std::move(value));
        }

        template<typename... Args>
        auto emplace(Args&&... args) ->void {
            std::unique_lock lock{mtx_};
            full_cv_.wait(lock, [&]() noexcept {
                return underlying_.size() != max_size_;
            });
            underlying_.emplace_back(std::forward<Args>(args)...);
            empty_cv_.notify_one();
        }

        [[nodiscard]]
        auto pop_value() noexcept(std::is_nothrow_move_constructible_v<value_type>) -> value_type {
            std::unique_lock lock{mtx_};
            empty_cv_.wait(lock, [this]() noexcept {
                return not underlying_.empty();
            });
            auto result = std::move(underlying_.front());
            underlying_.pop_front();
            full_cv_.notify_one();
            return result;
        }

        [[nodiscard]]
        auto try_pop_value() noexcept(std::is_nothrow_move_constructible_v<value_type>) -> std::optional<value_type> {
            std::unique_lock lock{mtx_};
            if (underlying_.empty()) return std::nullopt;
            auto result = std::move(underlying_.front());
            underlying_.pop_front();
            full_cv_.notify_one();
            return result;
        }

        [[nodiscard]]
        auto pop_all() noexcept(std::is_nothrow_move_constructible_v<container_type>) -> container_type {
            std::unique_lock lock{mtx_};
            container_type result = std::move(underlying_);
            underlying_.clear();
            return result;
        }

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            std::shared_lock _{mtx_};
            return underlying_.empty();
        }

        [[nodiscard]]
        auto size() const noexcept -> std::size_t {
            std::shared_lock _{mtx_};
            return underlying_.size();
        }
    private:
        Container underlying_;
        std::size_t max_size_ = underlying_.max_size();
        std::shared_mutex mtx_;
        std::condition_variable_any empty_cv_;
        std::condition_variable_any full_cv_;
    };


    /// single-producer, single-consumer queue
    template<typename T, typename BaseRange = std::vector<T>>
    class ring_buffer {
        static_assert(unqualified_object<T>, "type `T` shall be a cv-unqualified object-type.");
        static_assert(std::ranges::random_access_range<BaseRange>, "type `Range` shall be a random-access-range.");
        static_assert(std::default_initializable<T> and std::movable<T>, "type `T` shall be default-constructible and movable.");
        static_assert(std::same_as<std::ranges::range_value_t<BaseRange>, T>, "the value-type of `Range` shall be same as type `T`.");
    public:
        using value_type = T;
        using base_type = BaseRange;

    public:
        explicit ring_buffer(std::size_t capacity) requires requires { std::declval<BaseRange&>().resize(capacity); } : capacity_(capacity) {
            COIO_ASSERT(capacity > 0 and capacity <= max_capacity());
            range_.resize(capacity);
        }

        explicit ring_buffer(BaseRange range) noexcept(std::is_nothrow_move_constructible_v<BaseRange>) requires std::move_constructible<BaseRange> : ring_buffer(std::move(range), range.size()) {}

        ring_buffer(BaseRange range, std::size_t capacity) noexcept(std::is_nothrow_move_constructible_v<BaseRange>) requires std::move_constructible<BaseRange> : range_(std::move(range)), capacity_(capacity) {
            COIO_ASSERT(capacity > 0 and capacity <= max_capacity());
        }

        ring_buffer(const ring_buffer&) = delete;

        auto operator= (const ring_buffer&) -> ring_buffer& = delete;

        [[nodiscard]]
        static auto max_capacity() noexcept -> std::size_t {
            return std::numeric_limits<std::atomic_unsigned_lock_free::value_type>::max();
        }

        [[nodiscard]]
        auto capacity() const noexcept -> std::size_t {
            return capacity_;
        }

        [[nodiscard]]
        auto size() const noexcept -> std::size_t {
            return size_.load(std::memory_order_acquire);
        }

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return size() == 0;
        }

        [[nodiscard]]
        auto full() const noexcept -> bool {
            return size() == capacity_;
        }

        auto push(value_type value) noexcept(std::is_nothrow_move_constructible_v<value_type> and std::is_nothrow_move_assignable_v<value_type>) -> void {
            emplace(std::move(value));
        }

        [[nodiscard]]
        auto try_push(value_type value) noexcept(std::is_nothrow_move_constructible_v<value_type> and std::is_nothrow_move_assignable_v<value_type>) -> bool {
            return try_emplace(std::move(value));
        }

        template<typename... Args> requires std::constructible_from<value_type, Args...>
        auto emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...> and std::is_nothrow_assignable_v<value_type&, Args...>) -> void {
            size_.wait(capacity_); // wait: not full
            auto index = tail_.load(std::memory_order_relaxed);
            std::ranges::begin(range_)[index] = value_type(std::forward<Args>(args)...);
            tail_.store((index + 1) % capacity_, std::memory_order_release);
            size_.fetch_add(1, std::memory_order_acq_rel);
            size_.notify_one(); // notify: not empty
        }

        template<typename... Args> requires std::constructible_from<value_type, Args...>
        [[nodiscard]]
        auto try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...> and std::is_nothrow_assignable_v<value_type&, Args...>) -> bool {
            if (full()) return false;
            auto index = tail_.load(std::memory_order_relaxed);
            std::ranges::begin(range_)[index] = value_type(std::forward<Args>(args)...);
            tail_.store((index + 1) % capacity_, std::memory_order_release);
            size_.fetch_add(1, std::memory_order_acq_rel);
            size_.notify_one(); // notify: not empty
            return true;
        }

        [[nodiscard]]
        auto pop_value() noexcept(std::is_nothrow_move_constructible_v<value_type>) -> value_type {
            size_.wait(0); // wait: not empty
            auto index = head_.load(std::memory_order_relaxed);
            value_type result = std::move(std::ranges::begin(range_)[index]);
            head_.store((index + 1) % capacity_, std::memory_order_release);
            size_.fetch_sub(1, std::memory_order_acq_rel);
            size_.notify_one(); // notify: not full
            return result;
        }

        [[nodiscard]]
        auto try_pop_value(value_type& out) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> bool {
            if (empty()) return false;
            auto index = head_.load(std::memory_order_relaxed);
            out = std::move(std::ranges::begin(range_)[index]);
            head_.store((index + 1) % capacity_, std::memory_order_release);
            size_.fetch_sub(1, std::memory_order_acq_rel);
            size_.notify_one(); // notify: not full
            return true;
        }

        [[nodiscard]]
        auto try_pop_value() noexcept(std::is_nothrow_move_constructible_v<value_type>) -> std::optional<value_type> {
            if (empty()) return std::nullopt;
            auto index = head_.load(std::memory_order_relaxed);
            value_type result = std::move(std::ranges::begin(range_)[index]);
            head_.store((index + 1) % capacity_, std::memory_order_release);
            size_.fetch_sub(1, std::memory_order_acq_rel);
            size_.notify_one(); // notify: not full
            return std::optional<value_type>(std::move(result));
        }

    private:
        BaseRange range_;
        /* const */ std::size_t capacity_{};
        std::atomic_unsigned_lock_free size_{0};
        std::atomic<std::size_t> head_{0}, tail_{0};
    };

    template<std::ranges::random_access_range Range>
    explicit ring_buffer(Range rng) -> ring_buffer<std::ranges::range_value_t<Range>, Range>;

    template<std::ranges::random_access_range Range>
    ring_buffer(Range rng, std::size_t) -> ring_buffer<std::ranges::range_value_t<Range>, Range>;

}