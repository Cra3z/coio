#pragma once
#include <concepts>
#include <limits>
#include <optional>
#include <queue>
#include <type_traits>
#include <coio/utils/atomutex.h>
#include <coio/sync_primitives.h>

namespace coio {
    namespace detail {
        template<typename Q>
        concept queue_like =
            std::same_as<Q, std::decay_t<Q>> and
            requires (Q&& q) {
                typename Q::value_type;
                typename Q::size_type;
                q.push(std::declval<typename Q::value_type>());
                q.emplace(std::declval<typename Q::value_type>());
                q.pop();
                { q.front() } -> std::same_as<typename Q::reference>;
                { q.size() } -> std::same_as<typename Q::size_type>;
            };
    }

    template<typename T, typename Queue = std::queue<T>>
    class fifo {
        static_assert(unqualified_object<T>, "type `T` shall be a cv-unqualified object-type.");
        static_assert(
            std::move_constructible<T> and
            std::is_nothrow_move_constructible_v<T> and
            std::is_nothrow_destructible_v<T>,
            "type `T` shall be move-constructible."
        );
        static_assert(detail::queue_like<Queue>, "type `Container` shall model the concept `coio::detail::queue_like`.");
        static_assert(std::same_as<typename Queue::value_type, T>, "the value-type of `Container` shall be same as type `T`.");
    public:
        using container_type = Queue;
        using value_type = T;
        using size_type = typename Queue::size_type;

    public:
        template<typename... Args> requires std::constructible_from<value_type, Args...>
        explicit fifo(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>) :
            underlying_(std::forward<Args>(args)...),
            full_sema_(max_size()),
            empty_sema_(underlying_.size()) {}

        template<typename U, typename... Args> requires std::constructible_from<value_type, std::initializer_list<U>, Args...>
        explicit fifo(std::in_place_t, std::initializer_list<U> ilist, Args&&... args) noexcept(
            std::is_nothrow_constructible_v<value_type, std::initializer_list<U>, Args...>
        ) : underlying_(ilist, std::forward<Args>(args)...),
            full_sema_(max_size()),
            empty_sema_(underlying_.size()) {}

        fifo() noexcept(std::is_nothrow_default_constructible_v<value_type>)
            requires std::default_initializable<value_type> : fifo(std::in_place) {}

        fifo(const fifo&) = delete;

        ~fifo() {
            close();
            this_thread::sync_wait(scope_.join());
        }

        auto operator= (const fifo&) -> fifo& = delete;

        [[nodiscard]]
        auto empty() const noexcept -> bool {
            return size() == 0;
        }

        [[nodiscard]]
        auto size() const noexcept -> size_type {
            return empty_sema_.count();
        }

        [[nodiscard]]
        auto max_size() const noexcept -> size_type {
            if constexpr (requires { underlying_.max_size(); }) {
                return underlying_.max_size();
            }
            else {
                return std::numeric_limits<size_type>::max();
            }
        }

        [[nodiscard]]
        auto async_push(value_type value) {
            return this->async_emplace(std::move(value));
        }

        template<typename... Args>  requires std::constructible_from<value_type, Args...> and (... and std::move_constructible<Args>)
        [[nodiscard]]
        auto async_emplace(Args... args) {
            constexpr bool is_nothrow = noexcept(underlying_.emplace(std::move(args)...));
            return execution::then(
                execution::associate(full_sema_.acquire(), scope_.get_token()),
                [...args = std::move(args), this]() noexcept(is_nothrow) {
                    std::unique_lock guard{mtx_};
                    underlying_.emplace(std::move(args)...);
                    guard.unlock();
                    empty_sema_.release();
                }
            );
        }

        [[nodiscard]]
        auto async_pop() noexcept {
            return execution::then(
                execution::associate(empty_sema_.acquire(), scope_.get_token()),
                [this]() noexcept {
                    std::unique_lock guard{mtx_};
                    auto result = std::move(underlying_.front());
                    underlying_.pop();
                    guard.unlock();
                    full_sema_.release();
                    return result;
                }
            );
        }

        [[nodiscard]]
        auto try_push(value_type value) noexcept(noexcept(std::declval<fifo&>().try_emplace(std::move(value)))) -> bool {
            return this->try_emplace(std::move(value));
        }

        template<typename... Args>  requires std::constructible_from<value_type, Args...> and (... and std::move_constructible<Args>)
        [[nodiscard]]
        auto try_emplace(Args... args) noexcept(noexcept(std::declval<Queue&>().emplace(std::declval<Args>()...))) -> bool {
            if (not full_sema_.try_acquire()) return false;
            std::unique_lock guard{mtx_};
            underlying_.emplace(std::move(args)...);
            guard.unlock();
            empty_sema_.release();
            return true;
        }

        [[nodiscard]]
        auto try_pop() noexcept -> std::optional<value_type> {
            if (not empty_sema_.try_acquire()) return std::nullopt;
            std::unique_lock guard{mtx_};
            auto result = std::move(underlying_.front());
            underlying_.pop();
            guard.unlock();
            full_sema_.release();
            return result;
        }

        auto close() noexcept -> void {
            scope_.request_stop();
        }

    private:
        Queue underlying_;
        mutable async_semaphore<size_type> full_sema_;
        mutable async_semaphore<size_type> empty_sema_;
        mutable atomutex mtx_;
        execution::counting_scope scope_;
    };
}