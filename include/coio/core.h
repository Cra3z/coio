#pragma once
#include <ranges>
#include "execution_context.h"
#include "detail/intrusive_list.h"

namespace coio {
    namespace detail {
        template<typename T>
        using void_to_nothing = std::conditional_t<std::is_void_v<T>, nothing, T>;

        template<typename T>
        using awaitable_non_void_await_result_t = void_to_nothing<await_result_t<T>>;

        struct just_base {
            static auto await_ready() noexcept -> bool {
                return true;
            }

            static auto await_suspend(std::coroutine_handle<>) noexcept -> void {}
        };

        template<typename T>
        class just_t : public just_base {
        public:
            explicit just_t(T value) noexcept(std::is_nothrow_move_constructible_v<T>) : value_(std::move(value)) {}

            auto await_resume() noexcept(std::is_nothrow_move_constructible_v<T>) -> T {
                return std::move(value_);
            }

        private:
            T value_;
        };

        template<typename E>
        class just_error_t : public just_base {
        public:
            explicit just_error_t(E error) noexcept(std::is_nothrow_move_constructible_v<E>) : error_(std::move(error)) {}

            auto await_resume() noexcept(false) -> void {
                if constexpr (std::same_as<E, std::error_code>) {
                    throw std::system_error{error_};
                }
                if constexpr (std::same_as<E, std::exception_ptr>) {
                    COIO_ASSERT(error_ != nullptr);
                    std::rethrow_exception(std::move(error_));
                }
                else {
                    throw error_;
                }
            }

        private:
            E error_;
        };

        struct just_stopped_t {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<stoppable_promise Promise>
            static auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> std::coroutine_handle<> {
                return this_coro.promise().unhandled_stop();
            }

            static auto await_resume() noexcept -> void {}
        };

        struct just_fn {
            template<std::move_constructible T>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (T value) COIO_STATIC_CALL_OP_CONST noexcept(std::is_nothrow_move_constructible_v<T>) {
                return just_t{std::move(value)};
            }
        };

        struct just_error_fn {
            template<std::move_constructible E>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (E error) COIO_STATIC_CALL_OP_CONST noexcept(std::is_nothrow_move_constructible_v<E>) {
                return just_error_t{std::move(error)};
            }
        };

        struct just_stopped_fn {
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() () COIO_STATIC_CALL_OP_CONST noexcept {
                return just_stopped_t{};
            }
        };

        template<typename Awaiter, typename Fn>
        struct then_awaiter {
            decltype(auto) await_ready() noexcept(noexcept(std::declval<Awaiter&>().await_ready())) {
                return inner_.await_ready();
            }

            template<typename Promise> requires requires (Awaiter inner, std::coroutine_handle<Promise> coro) { inner.await_suspend(coro); }
            decltype(auto) await_suspend(std::coroutine_handle<Promise> this_coro) noexcept(noexcept(std::declval<Awaiter&>().await_suspend(this_coro))) {
                return inner_.await_suspend(this_coro);
            }

            decltype(auto) await_resume() noexcept(
                noexcept(std::declval<Awaiter&>().await_resume()) and
                std::is_nothrow_invocable_v<Fn, await_result_t<Awaiter&>>
            ) requires (not std::is_void_v<await_result_t<Awaiter&>>) {
                return std::invoke(std::move(fn_), inner_.await_resume());
            }

            decltype(auto) await_resume() noexcept(
                noexcept(std::declval<Awaiter&>().await_resume()) and
                std::is_nothrow_invocable_v<Fn>
            ) {
                inner_.await_resume();
                return std::invoke(std::move(fn_));
            }

            Awaiter inner_;
            Fn fn_;
        };

        template<typename Awaitable, typename Fn>
        class then_t {
        public:
            then_t(Awaitable awt, Fn fn) noexcept(std::is_nothrow_move_constructible_v<Awaitable> and std::is_nothrow_move_constructible_v<Fn>) :
                awt_(std::move(awt)), fn_(std::move(fn)) {}

            auto operator co_await() && noexcept(std::is_nothrow_invocable_v<get_awaiter_fn, Awaitable> and std::is_nothrow_move_constructible_v<Fn>) {
                return then_awaiter<Awaitable, Fn>{
                    get_awaiter(std::move(this->awt_)),
                    std::move(this->fn_)
                };
            }

        private:
            Awaitable awt_;
            Fn fn_;
        };

        struct then_fn {
            template<awaitable_value Awaitable, callable_<await_result_t<Awaitable>> Fn>
                requires std::move_constructible<Fn>
            COIO_STATIC_CALL_OP auto operator() (Awaitable awt, Fn fn) COIO_STATIC_CALL_OP_CONST {
                return then_t<Awaitable, Fn>{
                    std::move(awt),
                    std::move(fn)
                };
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
        class when_all_ready_t;

        template<elements_move_insertable_range WhenAllTasks> requires std::move_constructible<WhenAllTasks> and specialization_of<std::ranges::range_value_t<WhenAllTasks>, task_wrapper>
        class when_all_ready_t<WhenAllTasks> {
        public:
            when_all_ready_t(WhenAllTasks when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)), counter_(std::ranges::distance(when_all_tasks_)) {}

            auto await_ready() const noexcept -> bool {
                return bool(counter_.prev_coro);
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
                counter_.prev_coro = this_coro;
                for (auto& task : when_all_tasks_) {
                    task.coro_.promise().on_final_suspend_.counter_ = &counter_;
                    task.coro_.resume();
                }
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
        class when_all_ready_t<std::tuple<when_all_task<Types, Allocs>...>> {
        public:
            when_all_ready_t(std::tuple<when_all_task<Types, Allocs>...> when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)) {}

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
        when_all_ready_t(std::tuple<when_all_task<Types, Allocs>...>) -> when_all_ready_t<std::tuple<when_all_task<Types, Allocs>...>>;

        template<typename Alloc, typename Awaitable>
        auto do_when_all_task(std::allocator_arg_t, const Alloc&, Awaitable awaitable) -> when_all_task<await_result_t<Awaitable>, Alloc> {
            co_return co_await std::move(awaitable);
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
            template<awaitable_value... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) {
                return when_all_ready_t{
                    std::tuple{
                        (do_when_all_task)(std::forward<Awaitables>(awaitables))...
                    }
                };
            }

            template<typename Alloc, awaitable_value... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (std::allocator_arg_t, const Alloc& alloc, Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) {
                return when_all_ready_t{
                    std::tuple{
                        (do_when_all_task)(std::allocator_arg, alloc, std::forward<Awaitables>(awaitables))...
                    }
                };
            }

            template<std::forward_iterator It, std::sentinel_for<It> St> requires awaitable_value<std::iter_value_t<It>> and std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (It first, St last) COIO_STATIC_CALL_OP_CONST {
                using when_all_task_container_t = std::vector<
                    when_all_task<
                        await_result_t<std::iter_value_t<It>>
                    >
                >;
                when_all_task_container_t result;
                while (first != last) {
                    void(result.push_back((do_when_all_task)(*(first++))));
                }
                return when_all_ready_t<when_all_task_container_t>{std::move(result)};
            }

            template<borrowed_forward_range TaskRange> requires awaitable_value<std::ranges::range_value_t<TaskRange>> and std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>>
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
            template<awaitable_value... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) {
                return when_all_fn{}(std::allocator_arg, std::allocator<void>{}, std::forward<Awaitables>(awaitables)...);
            }

            template<typename Alloc, awaitable_value... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (std::allocator_arg_t, const Alloc& alloc, Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) {
                return then_fn{}(
                    when_all_ready_fn{}(std::allocator_arg, alloc, std::forward<Awaitables>(awaitables)...),
                    get_when_all_result{}
                );
            }

            template<std::forward_iterator It, std::sentinel_for<It> St, elements_move_insertable_range ResultStorageRange>
                requires awaitable_value<std::iter_value_t<It>> and
                        std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>> and
                        std::default_initializable<ResultStorageRange> and
                        std::move_constructible<ResultStorageRange> and
                        std::convertible_to<awaitable_non_void_await_result_t<std::iter_value_t<It>>, std::ranges::range_value_t<ResultStorageRange>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (It first, St last, store_results_in_t<ResultStorageRange>) COIO_STATIC_CALL_OP_CONST {
                return then_fn{}(
                    when_all_ready_fn{}(first, last),
                    []<typename T>(std::vector<when_all_task<T>> when_all_tasks_) {
                        ResultStorageRange result_storage_range;
                        for (auto& task : when_all_tasks_) {
                            void(result_storage_range.insert(std::ranges::end(result_storage_range), task.get_non_void_result()));
                        }
                        return result_storage_range;
                    }
                );
            }

            template<std::forward_iterator It, std::sentinel_for<It> St> requires awaitable_value<std::iter_value_t<It>> and std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (It first, St last) COIO_STATIC_CALL_OP_CONST {
                return (*this)(first, last, store_results_in<std::vector<awaitable_non_void_await_result_t<std::iter_value_t<It>>>>);
            }

            template<borrowed_forward_range TaskRange, elements_move_insertable_range ResultStorageRange>
                requires awaitable_value<std::ranges::range_value_t<TaskRange>> and
                        std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>> and
                        std::default_initializable<ResultStorageRange> and
                        std::move_constructible<ResultStorageRange> and
                        std::convertible_to<awaitable_non_void_await_result_t<std::ranges::range_value_t<TaskRange>>, std::ranges::range_value_t<ResultStorageRange>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng, store_results_in_t<ResultStorageRange>) COIO_STATIC_CALL_OP_CONST {
                return (*this)(std::ranges::begin(rng), std::ranges::end(rng), store_results_in<ResultStorageRange>);
            }

            template<borrowed_forward_range TaskRange> requires awaitable_value<std::ranges::range_value_t<TaskRange>> and std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>>
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
        auto do_sync_wait_task(Awaitable&& awaitable) -> sync_wait_task<await_result_t<Awaitable>> {
            co_return co_await std::forward<Awaitable>(awaitable);
        }

        struct sync_wait_fn {
            template<awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST -> await_result_t<Awaitable> {
                auto sync_ = (do_sync_wait_task)(std::forward<Awaitable>(awt));
                sync_.coro_.resume();
                sync_.coro_.promise().on_final_suspend_.finished_.wait(0);
                return static_cast<std::add_rvalue_reference_t<await_result_t<Awaitable>>>(sync_.get_result());
            }
        };
    }

    class async_scope : retain_base<async_scope> {
        friend retain_base;
        friend retain_ptr<async_scope>;
    public:
        struct token {}; // TODO: adaptation to p3149 (https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2025/p3149r11.html)

        class join_sender {
            friend async_scope;
        private:
            class awaiter {
                friend join_sender;
                friend async_scope;
            public:
                awaiter(async_scope& scope) noexcept : scope_(scope) {}

                awaiter(const awaiter&) = delete;

                auto operator= (const awaiter&) -> awaiter& = delete;

                auto await_ready() noexcept -> bool {
                    return scope_.ref_count_.load(std::memory_order_acquire) == 0;
                }

                auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> bool {
                    coro_ = this_coro;
                    scope_.list_.push(*this);
                    return scope_.ref_count_.fetch_sub(1, std::memory_order_acq_rel) > 1;
                }

                static auto await_resume() noexcept -> void {}

            private:
                async_scope& scope_;
                std::coroutine_handle<> coro_;
                awaiter* next_ = nullptr;
            };

        private:
            join_sender(async_scope& scope) noexcept : scope_(&scope) {}

        public:
            join_sender(const join_sender&) = delete;

            join_sender(join_sender&& other) noexcept : scope_(std::exchange(other.scope_, {})) {};

            auto operator= (const join_sender&) -> join_sender& = delete;

            auto operator= (join_sender&& other) noexcept -> join_sender& {
                scope_ = std::exchange(other.scope_, {});
                return *this;
            }

            auto operator co_await() && noexcept -> awaiter {
                return awaiter{*std::exchange(scope_, nullptr)};
            }

        private:
            async_scope* scope_;
        };

    public:
        async_scope() noexcept : retain_base(1) {}

        template<awaitable_value Awaitable>
        auto spawn(Awaitable&& awt) -> void {
            [](std::decay_t<Awaitable> awt_, retain_ptr<async_scope>) -> fire_and_forget {
                void(co_await std::forward<Awaitable>(awt_));
            }(std::forward<Awaitable>(awt), retain_ptr{this});
        }

        [[nodiscard]]
        auto get_token() noexcept -> token {
            return {};
        }

        [[nodiscard]]
        auto join() noexcept -> join_sender {
            return join_sender{*this};
        }

    private:
        auto do_lose() noexcept -> void {
            auto node = list_.pop_all();
            while (node) {
                auto next = node->next_;
                node->coro_.resume();
                node = next;
            }
        }

    private:
        detail::intrusive_list<join_sender::awaiter> list_{&join_sender::awaiter::next_};
    };

    using detail::store_results_in;

    inline constexpr detail::when_all_ready_fn           when_all_ready{};
    inline constexpr detail::when_all_fn                 when_all{};
    inline constexpr detail::sync_wait_fn                sync_wait{};
    inline constexpr detail::then_fn                     then{};
    inline constexpr detail::just_fn                     just{};
    inline constexpr detail::just_error_fn               just_error{};
    inline constexpr detail::just_stopped_fn             just_stopped{};
}
