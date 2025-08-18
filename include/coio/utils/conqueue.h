#pragma once
#include <atomic>
#include <concepts>
#include <deque>
#include <limits>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <type_traits>
#include <vector>
#include "../task.h"
#include "../sync_primitives.h"
#include "../utils/inplace_vector.h"

namespace coio {
    namespace detail {
        template<typename Alloc, typename C>
        auto dummy_conqueue_get_allocator_(C& c) {
            if constexpr (requires {typename C::allocator_type; }) {
                return c.get_allocator();
            }
            else return Alloc();
        }

        template<typename C, typename Alloc>
        concept valid_conqueue_container_ =
            std::same_as<C, std::decay_t<C>> and
            std::ranges::bidirectional_range<C> and
            requires (C&& c) {
                typename C::value_type;
                typename C::size_type;
                c.push_back(std::declval<typename C::value_type>());
                c.pop_front();
                c.clear();
                { c.max_size() } -> std::same_as<typename C::size_type>;
                requires std::convertible_to<
                    decltype((dummy_conqueue_get_allocator_<Alloc>)(c)),
                    Alloc
                >;
            };
    }

    template<typename T, typename Alloc = std::allocator<T>, typename Container = std::deque<T, Alloc>>
    class conqueue {
        static_assert(unqualified_object<T>, "type `T` shall be a cv-unqualified object-type.");
        static_assert(std::movable<T>, "type `T` shall be movable.");
        static_assert(detail::valid_coroutine_alloctor_<Alloc>, "type `Alloc` shall model the concept `coio::detail::valid_coroutine_allocator_`.");
        static_assert(detail::valid_conqueue_container_<Container, Alloc>, "type `Container` shall model the concept `coio::detail::valid_conqueue_container_`.");
        static_assert(std::same_as<typename Container::value_type, T>, "the value-type of `Container` shall be same as type `T`.");
    public:
        using container_type = Container;
        using value_type = T;
        using size_type = typename Container::size_type;
        using allocator_type = Alloc;

    public:
        conqueue() noexcept : capacity_(max_capacity()), full_sema_(max_capacity()), empty_sema_(0) {}

        /// \note if `capacity` is less than 1 or greater than `max_capacity()`, the behavior is undefined
        explicit conqueue(size_type capacity) noexcept : capacity_(capacity), full_sema_(capacity), empty_sema_(0) {}

        conqueue(const conqueue&) = delete;

        auto operator= (const conqueue&) -> conqueue& = delete;

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return size() == 0;
        }

        [[nodiscard]]
        auto size() const noexcept -> size_type {
            return empty_sema_.count();
        }

        [[nodiscard]]
        auto capacity() const noexcept -> size_type {
            return capacity_;
        }

        [[nodiscard]]
        auto max_capacity() const noexcept -> size_type {
            using int_type = std::common_type_t<async_semaphore<>::count_type, size_type>;
            return std::min<int_type>(async_semaphore<>::max(), container_.max_size());
        }

        [[nodiscard]]
        auto get_allocator() const noexcept -> allocator_type {
            if constexpr (requires { typename Container::allocator_type; } ) {
                return container_.get_allocator();
            }
            else {
                return allocator_type();
            }
        }

        auto push(value_type value) {
            return this->emplace(std::move(value));
        }

        template<typename... Args>  requires std::constructible_from<value_type, Args...>
        auto emplace(Args&&... args) {
            return this->emplace(std::allocator_arg, get_allocator(), std::forward<Args>(args)...);
        }

        template<typename OtherAlloc, typename... Args> requires std::constructible_from<value_type, Args...>
        auto emplace(std::allocator_arg_t, const OtherAlloc&, Args&&... args) -> task<void, OtherAlloc> {
            co_await full_sema_.acquire();
            {
                auto _ = co_await mtx_.lock_guard();
                container_.emplace_back(std::forward<Args>(args)...);
            }
            empty_sema_.release();
        }

        auto pop() {
            return this->pop(std::allocator_arg, get_allocator());
        }

        template<typename OtherAlloc>
        auto pop(std::allocator_arg_t, const OtherAlloc&) -> task<value_type, OtherAlloc> {
            std::optional<value_type> result;
            co_await empty_sema_.acquire();
            {
                auto _ = co_await mtx_.lock_guard();
                result.emplace(container_.front());
                container_.pop_front();
            }
            full_sema_.release();
            co_return *result;
        }

        auto try_pop() {
            return this->try_pop(std::allocator_arg, get_allocator());
        }

        template<typename OtherAlloc>
        auto try_pop(std::allocator_arg_t, const OtherAlloc&) -> task<std::optional<value_type>, OtherAlloc> {
            if (not empty_sema_.try_acquire()) co_return std::nullopt;
            std::optional<value_type> result;
            {
                auto _ = co_await mtx_.lock_guard();
                result.emplace(container_.front());
                container_.pop_front();
            }
            full_sema_.release();
            co_return *result;
        }

        auto pop_all() {
            return this->pop_all(std::allocator_arg, get_allocator());
        }

        template<typename OtherAlloc>
        auto pop_all(std::allocator_arg_t, const OtherAlloc&) -> task<container_type, OtherAlloc> {
            auto _ = co_await mtx_.lock_guard();
            container_type result = std::move(container_);
            container_.clear();
            co_return result;
        }



    private:
        Container container_;
        const size_type capacity_;
        mutable async_semaphore<> full_sema_;
        mutable async_semaphore<> empty_sema_;
        mutable async_mutex mtx_;
    };

    /// single-producer, single-consumer queue
    template<typename T, typename Alloc = std::allocator<T>, typename Container = std::vector<T, Alloc>>
    class ring_buffer {
        static_assert(unqualified_object<T>, "type `T` shall be a cv-unqualified object-type.");
        static_assert(std::ranges::random_access_range<Container>, "type `Container` shall be a random-access-range.");
        static_assert(std::default_initializable<T> and std::movable<T>, "type `T` shall be default-constructible and movable.");
        static_assert(std::same_as<typename Container::value_type, T>, "the value-type of `Container` shall be same as type `T`.");
    public:
        using value_type = T;
        using container_type = Container;
        using size_type = typename Container::size_type;
        using allocator_type = Alloc;

    public:
        explicit ring_buffer(std::size_t capacity) : capacity_(capacity), full_sema_(capacity), empty_sema_(0) {
            COIO_ASSERT(capacity > 0 and capacity <= max_capacity());
            container_.resize(capacity);
        }

        ring_buffer(const ring_buffer&) = delete;

        auto operator= (const ring_buffer&) -> ring_buffer& = delete;

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return size() == 0;
        }

        [[nodiscard]]
        auto full() const noexcept -> bool {
            return size() == capacity_;
        }

        [[nodiscard]]
        auto size() const noexcept -> size_type {
            return empty_sema_.count();
        }

        [[nodiscard]]
        auto capacity() const noexcept -> size_type {
            return capacity_;
        }

        [[nodiscard]]
        auto max_capacity() const noexcept -> size_type {
            return std::numeric_limits<std::atomic_unsigned_lock_free::value_type>::max();
        }

        [[nodiscard]]
        auto get_allocator() const noexcept -> allocator_type {
            if constexpr (requires { typename Container::allocator_type; } ) {
                return container_.get_allocator();
            }
            else {
                return allocator_type();
            }
        }

        auto push(value_type value) {
            this->emplace(std::move(value));
        }

        template<typename... Args> requires std::constructible_from<value_type, Args...>
        auto emplace(Args&&... args) {
            return this->emplace(std::allocator_arg, get_allocator(), std::forward<Args>(args)...);
        }

        template<typename OtherAlloc, typename... Args> requires std::constructible_from<value_type, Args...>
        auto emplace(std::allocator_arg_t, const OtherAlloc&, Args&&... args) -> task<void, OtherAlloc> {
            co_await full_sema_.acquire(); // wait: not full
            auto index = tail_.load(std::memory_order_relaxed);
            std::ranges::begin(container_)[index] = value_type(std::forward<Args>(args)...);
            tail_.store((index + 1) % capacity_, std::memory_order_release);
            empty_sema_.release();
        }

        auto pop() {
            return this->pop(std::allocator_arg, get_allocator());
        }

        template<typename OtherAlloc>
        auto pop(std::allocator_arg_t, const OtherAlloc&) -> task<value_type, OtherAlloc> {
            co_await empty_sema_.acquire(); // wait: not empty
            auto index = head_.load(std::memory_order_relaxed);
            value_type result = std::move(std::ranges::begin(container_)[index]);
            head_.store((index + 1) % capacity_, std::memory_order_release);
            full_sema_.release(); // notify: not full
            co_return result;
        }

        auto try_pop() {
            return this->try_pop(std::allocator_arg, get_allocator());
        }

        template<typename OtherAlloc>
        auto try_pop(std::allocator_arg_t, const OtherAlloc&) -> task<std::optional<value_type>, OtherAlloc> {
            if (empty()) return std::nullopt;
            auto index = head_.load(std::memory_order_relaxed);
            value_type result = std::move(std::ranges::begin(container_)[index]);
            head_.store((index + 1) % capacity_, std::memory_order_release);
            full_sema_.release(); // notify: not full
            return std::optional<value_type>(std::move(result));
        }

    private:
        Container container_;
        /* const */ size_type capacity_{};
        std::atomic<async_semaphore<>::count_type> head_{0}, tail_{0};
        mutable async_semaphore<> full_sema_;
        mutable async_semaphore<> empty_sema_;
    };

    template<typename T, std::size_t N>
    using inplace_ring_buffer = ring_buffer<T, std::allocator<T>, inplace_vector<T, N>>;

}