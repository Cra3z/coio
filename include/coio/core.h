#pragma once
#include <ranges>
#include "execution_context.h"

namespace coio {
    namespace detail {
        template<typename T>
        using void_to_nothing = std::conditional_t<std::is_void_v<T>, nothing, T>;

        template<typename T>
        using awaitable_non_void_await_result_t = void_to_nothing<awaitable_await_result_t<T>>;
    }

    template<typename Awaiter, typename Fn>
    struct transform_awaiter {

        static_assert(awaiter<Awaiter>);
        static_assert(std::invocable<Fn, await_result_t<Awaiter>>);

        auto await_ready() noexcept(noexcept(std::declval<Awaiter&>().await_ready())) {
            return inner_awaiter.await_ready();
        }

        auto await_suspend(std::coroutine_handle<> this_coro) noexcept(noexcept(std::declval<Awaiter&>().await_suspend(std::declval<std::coroutine_handle<>>()))) {
            return inner_awaiter.await_suspend(this_coro);
        }

        decltype(auto) await_resume() noexcept(noexcept(std::invoke(std::declval<Fn>(), std::declval<Awaiter&>().await_resume()))) {
            return std::invoke(transform, inner_awaiter.await_resume());
        }

        Awaiter inner_awaiter;
        Fn transform;
    };

    namespace detail {
        struct make_transform_awaiter_fn {
            template<awaiter Awaiter, std::invocable<await_result_t<Awaiter>> Fn> requires std::move_constructible<Awaiter> and std::move_constructible<Fn>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaiter awaiter, Fn fn) COIO_STATIC_CALL_OP_CONST noexcept(std::is_nothrow_move_constructible_v<Awaiter> and std::is_nothrow_move_constructible_v<Fn>) ->transform_awaiter<Awaiter, Fn> {
                return transform_awaiter{std::move(awaiter), std::move(fn)};
            }
        };

        struct when_all_counter {
            using count_type = std::atomic_unsigned_lock_free::value_type;

            explicit when_all_counter(std::size_t count) noexcept : count(count) {
                COIO_ASSERT(count > 0);
            }

            [[nodiscard]]
            auto decrease() noexcept -> std::coroutine_handle<> {
                std::atomic_ref<count_type> count_ref{count};
                if (--count_ref == 0) return prev_coro;
                return std::noop_coroutine();
            }

            count_type count;
            std::coroutine_handle<> prev_coro;
        };

        template<typename T, typename OnFinalSuspend, typename Alloc = void>
        struct task_wrapper {
            static_assert(std::default_initializable<OnFinalSuspend> and std::is_nothrow_invocable_r_v<std::coroutine_handle<>, OnFinalSuspend>);
            static_assert(
                detail::valid_coroutine_alloctor_<Alloc>,
                "typename `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
            );

            struct promise_type : promise_return_control<T>, promise_alloc_control<Alloc> {
                promise_type() = default;

                auto get_return_object() noexcept -> task_wrapper {
                    return {std::coroutine_handle<promise_type>::from_promise(*this)};
                }

                static auto initial_suspend() noexcept -> std::suspend_always { return {}; }

                auto final_suspend() noexcept -> task_final_awaiter {
                    return {on_final_suspend_()};
                }

                OnFinalSuspend on_final_suspend_;
            };

            task_wrapper(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro) {}

            task_wrapper(const task_wrapper&) = delete;

            task_wrapper(task_wrapper&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

            ~task_wrapper() {
                if (coro_) coro_.destroy();
            }

            auto operator= (const task_wrapper&) = delete;

            auto operator= (task_wrapper&& other) noexcept -> task_wrapper& {
                task_wrapper(std::move(other)).swap(*this);
                return *this;
            }

            auto swap(task_wrapper& other) noexcept -> void {
                std::swap(coro_, other.coro_);
            }

            friend auto swap(task_wrapper& lhs, task_wrapper& rhs) noexcept -> void {
                lhs.swap(rhs);
            }

            decltype(auto) get_result() const {
                return coro_.promise().get_result();
            }

            decltype(auto) get_non_void_result() const {
                if constexpr (std::is_void_v<T>) {
                    get_result();
                    return nothing{};
                }
                else return get_result();;
            }

            std::coroutine_handle<promise_type> coro_;
        };

        struct on_when_all_task_final_suspend_fn {
            COIO_STATIC_CALL_OP auto operator() () COIO_STATIC_CALL_OP_CONST noexcept -> std::coroutine_handle<> {
                COIO_ASSERT(counter_ != nullptr);
                return counter_->decrease();
            }
            when_all_counter* counter_ = nullptr;
        };

        template<typename T, typename Alloc = void>
        using when_all_task = task_wrapper<T, on_when_all_task_final_suspend_fn, Alloc>;

        template<typename>
        class when_all_ready_awaiter;

        template<elements_move_insertable_range WhenAllTasks> requires std::move_constructible<WhenAllTasks> and specialization_of<std::ranges::range_value_t<WhenAllTasks>, task_wrapper>
        class when_all_ready_awaiter<WhenAllTasks> {
        public:
            when_all_ready_awaiter(WhenAllTasks when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)), counter_(std::ranges::distance(when_all_tasks_)) {}

            auto await_ready() const noexcept -> bool {
                return bool(counter_.prev_coro);
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
                counter_.prev_coro = this_coro;
                for (auto& task : when_all_tasks_) {
                    task.coro_.promise().on_final_suspend_.counter_ = &counter_;
                    task.coro_.resume();
                };
            }

            [[nodiscard]]
            auto await_resume() noexcept(std::is_nothrow_move_constructible_v<WhenAllTasks>) -> WhenAllTasks {
                return std::move(when_all_tasks_);
            }

        private:
            WhenAllTasks when_all_tasks_;
            when_all_counter counter_;
        };

        template<typename... Types, typename... Allocs>
        class when_all_ready_awaiter<std::tuple<when_all_task<Types, Allocs>...>> {
        public:
            when_all_ready_awaiter(std::tuple<when_all_task<Types, Allocs>...> when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)) {}

            auto await_ready() const noexcept -> bool {
                return bool(counter_.prev_coro);
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
                counter_.prev_coro = this_coro;
                [this]<std::size_t... I>(std::index_sequence<I...>) {
                    (..., (std::get<I>(when_all_tasks_).coro_.promise().on_final_suspend_.counter_ = &counter_));
                    (..., std::get<I>(when_all_tasks_).coro_.resume());
                }(std::make_index_sequence<sizeof...(Types)>{});
            }

            [[nodiscard]]
            auto await_resume() noexcept(std::is_nothrow_move_constructible_v<std::tuple<when_all_task<Types, Allocs>...>>) -> std::tuple<when_all_task<Types, Allocs>...> {
                return std::move(when_all_tasks_);
            }

        private:
            std::tuple<when_all_task<Types, Allocs>...> when_all_tasks_;
            when_all_counter counter_{sizeof...(Types)};
        };

        template<typename... Types, typename... Allocs>
        when_all_ready_awaiter(std::tuple<when_all_task<Types, Allocs>...>) -> when_all_ready_awaiter<std::tuple<when_all_task<Types, Allocs>...>>;

        template<typename Alloc, typename Awaitable>
        auto do_when_all_task(std::allocator_arg_t, const Alloc&, Awaitable awaitable) -> when_all_task<awaitable_await_result_t<Awaitable>, Alloc> {
            if constexpr (std::is_void_v<awaitable_await_result_t<Awaitable>>) {
                co_await awaitable;
                co_return;
            }
            else co_return co_await awaitable;
        }

        template<typename Awaitable>
        auto do_when_all_task(Awaitable awaitable) {
            return (do_when_all_task)(std::allocator_arg, std::allocator<void>{}, std::move(awaitable));
        }

        template<typename ResultStorageRange>
        struct store_results_in_t {};

        template<unqualified_object TaskStorageRange>
        inline constexpr store_results_in_t<TaskStorageRange> store_results_in{};

        struct when_all_ready_fn {
            template<awaitable... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
                return when_all_ready_awaiter{
                    std::tuple{
                        (do_when_all_task)(std::forward<Awaitables>(awaitables))...
                    }
                };
            }

            template<typename Alloc, awaitable... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (std::allocator_arg_t, const Alloc& alloc, Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
                return when_all_ready_awaiter{
                    std::tuple{
                        (do_when_all_task)(std::allocator_arg, alloc, std::forward<Awaitables>(awaitables))...
                    }
                };
            }

            template<std::forward_iterator It, std::sentinel_for<It> St> requires awaitable<std::iter_value_t<It>> and std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (It first, St last) COIO_STATIC_CALL_OP_CONST {
                using when_all_task_container_t = std::vector<
                    when_all_task<
                        awaitable_await_result_t<std::iter_value_t<It>>
                    >
                >;
                when_all_task_container_t result;
                while (first != last) {
                    void(result.push_back((do_when_all_task)(*(first++))));
                }
                return when_all_ready_awaiter<when_all_task_container_t>{std::move(result)};
            }

            template<borrowed_forward_range TaskRange> requires awaitable<std::ranges::range_value_t<TaskRange>> and std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng) COIO_STATIC_CALL_OP_CONST {
                return (*this)(std::ranges::begin(rng), std::ranges::end(rng));
            }

        };

        template<typename OutTypeList, typename OutIndexSeq, typename TypeList, typename IndexSeq>
        struct non_void_impl;

        template<typename... OutTypes, std::size_t... OutIdx, typename First, typename... Rest, std::size_t I, std::size_t... Idx>
        struct non_void_impl<type_list<OutTypes...>, std::index_sequence<OutIdx...>, type_list<First, Rest...>, std::index_sequence<I, Idx...>> :
            non_void_impl<type_list<OutTypes..., First>, std::index_sequence<OutIdx..., I>, type_list<Rest...>, std::index_sequence<Idx...>> {};

        template<typename... OutTypes, std::size_t... OutIdx, typename... Rest, std::size_t I, std::size_t... Idx>
        struct non_void_impl<type_list<OutTypes...>, std::index_sequence<OutIdx...>, type_list<void, Rest...>, std::index_sequence<I, Idx...>> :
            non_void_impl<type_list<OutTypes...>, std::index_sequence<OutIdx...>, type_list<Rest...>, std::index_sequence<Idx...>> {};

        template<typename... OutTypes, std::size_t... Idx>
        struct non_void_impl<type_list<OutTypes...>, std::index_sequence<Idx...>, type_list<>, std::index_sequence<>> {
            using types = std::tuple<OutTypes...>;
            using indices = std::index_sequence<Idx...>;
        };

        template<typename... Types>
        using non_void = non_void_impl<type_list<>, std::index_sequence<>, type_list<Types...>, std::make_index_sequence<sizeof...(Types)>>;

        struct get_when_all_result {
            template<typename ReturnType, std::size_t... I, typename RefTuple>
            static auto filter_(std::index_sequence<I...>, RefTuple tpl) -> ReturnType {
                return ReturnType(std::get<I>(std::move(tpl))...);
            }

            template<typename... Types, typename... Allocs>
            COIO_STATIC_CALL_OP auto operator()(std::tuple<when_all_task<Types, Allocs>...> when_all_tasks_) COIO_STATIC_CALL_OP_CONST {
                using NonVoidTypes = typename non_void<Types...>::types;
                using NonVoidIndices = typename non_void<Types...>::indices;
                using ReturnType = std::conditional_t<std::same_as<NonVoidTypes, std::tuple<>>, void, NonVoidTypes>;
                return [&when_all_tasks_]<std::size_t... I>(std::index_sequence<I...>) {
                    return (filter_<ReturnType>)(
                        NonVoidIndices{},
                        std::forward_as_tuple(std::get<I>(when_all_tasks_).get_non_void_result()...)
                    );
                }(std::index_sequence_for<Types...>{});
            }
        };

        struct when_all_fn {
            template<awaitable... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
                return when_all_fn{}(std::allocator_arg, std::allocator<void>{}, std::forward<Awaitables>(awaitables)...);
            }

            template<typename Alloc, awaitable... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (std::allocator_arg_t, const Alloc& alloc, Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
                return transform_awaiter{
                    when_all_ready_fn{}(std::allocator_arg, alloc, std::forward<Awaitables>(awaitables)...),
                    get_when_all_result{}
                };
            }

            template<std::forward_iterator It, std::sentinel_for<It> St, elements_move_insertable_range ResultStorageRange>
                requires awaitable<std::iter_value_t<It>> and
                        std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>> and
                        std::default_initializable<ResultStorageRange> and
                        std::move_constructible<ResultStorageRange> and
                        std::convertible_to<awaitable_non_void_await_result_t<std::iter_value_t<It>>, std::ranges::range_value_t<ResultStorageRange>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (It first, St last, store_results_in_t<ResultStorageRange>) COIO_STATIC_CALL_OP_CONST {
                return transform_awaiter{
                    when_all_ready_fn{}(first, last),
                    []<typename T>(std::vector<when_all_task<T>> when_all_tasks_) {
                        ResultStorageRange result_storage_range;
                        for (auto& task : when_all_tasks_) {
                            void(result_storage_range.insert(std::ranges::end(result_storage_range), task.get_non_void_result()));
                        }
                        return result_storage_range;
                    }
                };
            }

            template<std::forward_iterator It, std::sentinel_for<It> St> requires awaitable<std::iter_value_t<It>> and std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (It first, St last) COIO_STATIC_CALL_OP_CONST {
                return (*this)(first, last, store_results_in<std::vector<awaitable_non_void_await_result_t<std::iter_value_t<It>>>>);
            }

            template<borrowed_forward_range TaskRange, elements_move_insertable_range ResultStorageRange>
                requires awaitable<std::ranges::range_value_t<TaskRange>> and
                        std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>> and
                        std::default_initializable<ResultStorageRange> and
                        std::move_constructible<ResultStorageRange> and
                        std::convertible_to<awaitable_non_void_await_result_t<std::ranges::range_value_t<TaskRange>>, std::ranges::range_value_t<ResultStorageRange>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng, store_results_in_t<ResultStorageRange>) COIO_STATIC_CALL_OP_CONST {
                return (*this)(std::ranges::begin(rng), std::ranges::end(rng), store_results_in<ResultStorageRange>);
            }

            template<borrowed_forward_range TaskRange> requires awaitable<std::ranges::range_value_t<TaskRange>> and std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng) COIO_STATIC_CALL_OP_CONST {
                return (*this)(std::ranges::begin(rng), std::ranges::end(rng));
            }
        };

        struct on_sync_wait_final_suspend_fn {
            COIO_STATIC_CALL_OP auto operator()() noexcept -> std::coroutine_handle<> {
                finished_ = 1;
                finished_.notify_all();
                return std::noop_coroutine();
            }
            std::atomic_unsigned_lock_free finished_{0};
        };

        template<typename T, typename Alloc = void>
        using sync_wait_task = task_wrapper<T, on_sync_wait_final_suspend_fn, Alloc>;

        template<typename Awaitable>
        auto do_sync_wait_task(Awaitable awaitable) -> sync_wait_task<awaitable_await_result_t<Awaitable>> {
            if constexpr (std::is_void_v<awaitable_await_result_t<Awaitable>>) {
                co_await awaitable;
                co_return;
            }
            else co_return co_await awaitable;
        }

        struct sync_wait_fn {
            template<awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST -> awaitable_await_result_t<Awaitable> {
                auto sync_ = (do_sync_wait_task)(std::forward<Awaitable>(awt));
                sync_.coro_.resume();
                sync_.coro_.promise().on_final_suspend_.finished_.wait(0);
                return static_cast<std::add_rvalue_reference_t<awaitable_await_result_t<Awaitable>>>(sync_.get_result());
            }
        };
    }

    class async_scope : retain_base<async_scope> {
        friend retain_base;
        friend retain_ptr<async_scope>;
    public:
        async_scope() noexcept : retain_base(1) {}

        template<awaitable Awaitable>
        auto spawn(Awaitable&& awt) -> void {
            [](std::decay_t<Awaitable> awt_, retain_ptr<async_scope>) -> fire_and_forget {
                void(co_await awt_);
            }(std::forward<Awaitable>(awt), retain_ptr{this});
        }

        auto await_ready() noexcept -> bool {
            return ref_count_.load(std::memory_order_acquire) == 0;
        }

        auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> bool {
            continuation_ = this_coro;
            return ref_count_.fetch_sub(1, std::memory_order_acq_rel) > 1;
        }

        static auto await_resume() noexcept -> void {}

    private:
        auto do_lose() noexcept -> void {
            continuation_.resume();
        }

    private:
        std::coroutine_handle<> continuation_;
    };

    template<typename T>
    class awaitable_reference_wrapper {
         static_assert(std::is_object_v<T> and awaitable<T>, "type `T` shall be an awaitable-type.");
    public:
        constexpr awaitable_reference_wrapper(T& obj) noexcept : ptr_(std::addressof(obj)) {}

        decltype(auto) operator co_await() const noexcept(noexcept(detail::to_awaiter_fn{}(std::declval<T&>()))) {
            return detail::to_awaiter_fn{}(*ptr_);
        }

    private:
        std::add_pointer_t<T> ptr_;
    };

    namespace detail {
        struct awaitable_ref_fn {
            template<awaitable Awaitable>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaitable& awt) COIO_STATIC_CALL_OP_CONST noexcept -> awaitable_reference_wrapper<Awaitable> {
                return {awt};
            }
            COIO_STATIC_CALL_OP auto operator() (const auto&& awt) COIO_STATIC_CALL_OP_CONST -> void = delete;
        };
    }

    using detail::store_results_in;

    inline constexpr detail::when_all_ready_fn           when_all_ready{};
    inline constexpr detail::when_all_fn                 when_all{};
    inline constexpr detail::make_transform_awaiter_fn   make_transform_awaiter{};
    inline constexpr detail::sync_wait_fn                sync_wait{};
    inline constexpr detail::awaitable_ref_fn            awaitable_ref{};
    inline constexpr detail::to_awaiter_fn               to_awaiter{};
}
