#pragma once
#include <array>
#include <ranges>
#include "execution_context.h"
#include "detail/intrusive_stack.h"

namespace coio {
    namespace detail {
        template<typename T>
        using void_to_nothing = std::conditional_t<std::is_void_v<T>, nothing, T>;

        template<typename T>
        using awaitable_non_void_await_result_t = void_to_nothing<await_result_t<T>>;

        template<simple_promise Promise>
        class get_promise {
        public:
            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            COIO_ALWAYS_INLINE auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> std::coroutine_handle<> {
                promise_ = &this_coro.promise();
                return this_coro;
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto await_resume() noexcept -> Promise& {
                COIO_ASSERT(promise_ != nullptr);
                return *promise_;
            }

        private:
            Promise* promise_ = nullptr;
        };

        class get_coroutine_handle {
        public:
            COIO_ALWAYS_INLINE static auto await_ready() noexcept -> bool {
                return false;
            }

            COIO_ALWAYS_INLINE auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> std::coroutine_handle<> {
                coro_ = this_coro;
                return this_coro;
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto await_resume() noexcept -> std::coroutine_handle<> {
                return coro_;
            }

        private:
            std::coroutine_handle<> coro_;
        };

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
                return this_coro.promise().unhandled_stopped();
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
                return then_awaiter{
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

        template<typename Promise>
        struct basic_task {
            using promise_type = Promise;

            basic_task(std::coroutine_handle<promise_type> coro) noexcept : handle(coro) {}

            basic_task(const basic_task&) = delete;

            basic_task(basic_task&& other) noexcept : handle(std::exchange(other.handle, {})) {}

            ~basic_task() {
                if (handle) handle.destroy();
            }

            auto operator= (basic_task other) noexcept -> basic_task& {
                this->swap(other);
                return *this;
            }

            auto swap(basic_task& other) noexcept -> void {
                std::swap(handle, other.handle);
            }

            friend auto swap(basic_task& lhs, basic_task& rhs) noexcept -> void {
                lhs.swap(rhs);
            }

            decltype(auto) get_result() const {
                return handle.promise().get_result();
            }

            decltype(auto) try_get_result() const {
                return handle.promise().try_get_result();
            }

            decltype(auto) get_non_void_result() const {
                return handle.promise().get_non_void_result();
            }

            [[nodiscard]]
            auto release() noexcept -> std::coroutine_handle<> {
                return std::exchange(handle, {});
            }

            std::coroutine_handle<promise_type> handle;
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

        struct when_all_state {
            using count_type = std::atomic_unsigned_lock_free;

            explicit when_all_state(count_type::value_type count) noexcept : count(count) {
                COIO_ASSERT(count > 0);
            }

            [[nodiscard]]
            auto arrive() noexcept -> std::coroutine_handle<> {
                if (--count == 0) {
                    if (stopped_.load(std::memory_order_relaxed)) {
                        return stopped_callback_(continuation);
                    }
                    return continuation;
                }
                return std::noop_coroutine();
            }

            [[nodiscard]]
            auto cancel() noexcept -> std::coroutine_handle<> {
                stopped_.store(true, std::memory_order_relaxed);
                return arrive();
            }

            count_type count;
            std::coroutine_handle<> continuation;
            unhandled_stopped_fn stopped_callback_= &default_unhandled_stopped_;
            std::atomic<bool> stopped_{false};
        };

        template<typename... WhenAllPromises>
        COIO_ALWAYS_INLINE auto set_when_all_state(
            std::array<std::coroutine_handle<>, sizeof...(WhenAllPromises)>& coros,
            when_all_state& state
        ) noexcept -> void {
            [&coros, &state]<std::size_t... I>(std::index_sequence<I...>) noexcept {
                (
                    ...,
                    (
                        std::coroutine_handle<WhenAllPromises>::from_address(
                            coros[I].address()
                        ).promise().state_ = &state
                    )
                );
                (..., (coros[I].resume()));
            }(std::index_sequence_for<WhenAllPromises...>{});
        }

        template<typename ReturnType, std::size_t... I, typename RefTuple>
        COIO_ALWAYS_INLINE auto filter_void_(std::index_sequence<I...>, RefTuple tpl) -> ReturnType {
            return ReturnType(std::get<I>(std::move(tpl))...);
        }

        template<typename... WhenAllPromises>
        COIO_ALWAYS_INLINE auto get_when_all_result(
            std::array<std::coroutine_handle<>, sizeof...(WhenAllPromises)>& coros
        ) {
            using NonVoidTypes = typename non_void<typename WhenAllPromises::value_type...>::types;
            using NonVoidIndices = typename non_void<typename WhenAllPromises::value_type...>::indices;
            using ReturnType = std::conditional_t<std::same_as<NonVoidTypes, std::tuple<>>, void, NonVoidTypes>;
            return (filter_void_<ReturnType>)(
                NonVoidIndices{},
                [&coros]<std::size_t... I>(std::index_sequence<I...>) {
                    return std::forward_as_tuple(
                        std::coroutine_handle<WhenAllPromises>::from_address(
                            coros[I].address()
                        ).promise().get_non_void_result()...
                    );
                }(std::index_sequence_for<WhenAllPromises...>{})
            );
        }

        template<typename T, typename Alloc>
        struct when_all_promise : promise_return_control<T>, promise_alloc_control<Alloc> {
            using value_type = T;

            when_all_promise() = default;

            auto get_return_object() noexcept -> basic_task<when_all_promise> {
                return {std::coroutine_handle<when_all_promise>::from_promise(*this)};
            }

            auto unhandled_stopped() noexcept -> std::coroutine_handle<> {
                COIO_ASSERT(state_ != nullptr);
                return state_->cancel();
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() noexcept -> task_final_awaiter {
                COIO_ASSERT(state_ != nullptr);
                return {state_->arrive()};
            }

            when_all_state* state_ = nullptr;
        };

        template<typename T, typename Alloc = void>
        using when_all_task = basic_task<when_all_promise<T, Alloc>>;

        template<typename... Types>
        class when_all_t {
        private:
            using non_void_types = typename non_void<Types...>::types;
            using non_void_indices = typename non_void<Types...>::indices;
            using return_type = std::conditional_t<std::same_as<non_void_types, std::tuple<>>, void, non_void_types>;
            using coros = std::array<std::coroutine_handle<>, sizeof...(Types)>;
            using set_counter_fn = void(*)(coros&, when_all_state&) noexcept;
            using get_result_fn = return_type(*)(coros&);

            struct awaiter {
                static auto await_ready() noexcept -> bool {
                    return false;
                }

                template<typename Promise>
                auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> void {
                    COIO_ASSERT(state_.continuation == nullptr);
                    if constexpr (stoppable_promise<Promise>) {
                        state_.stopped_callback_ = &stop_stoppable_coroutine_<Promise>;
                    }
                    state_.continuation = this_coro;
                    when_all_.set_state_(when_all_.coros_, state_);
                }

                auto await_resume() {
                    return when_all_.get_result_(when_all_.coros_);
                }

                when_all_t when_all_;
                when_all_state state_{sizeof...(Types)};
            };

        public:
            template<typename... WhenAllTasks>
            when_all_t(WhenAllTasks... when_all_tasks) noexcept :
                coros_{when_all_tasks.release()...},
                set_state_(set_when_all_state<typename WhenAllTasks::promise_type...>),
                get_result_(get_when_all_result<typename WhenAllTasks::promise_type...>)
            {}

            when_all_t(const when_all_t&) = delete;

            when_all_t(when_all_t&& other) noexcept :
                coros_(std::exchange(other.coros_, {})),
                set_state_(std::exchange(other.set_state_, {})),
                get_result_(std::exchange(other.get_result_, {}))
            {}

            ~when_all_t() {
                for (auto coro : coros_) {
                    if (coro) coro.destroy();
                }
            }

            auto operator= (const when_all_t&) -> when_all_t& = delete;

            auto operator= (when_all_t&& other) noexcept -> when_all_t& {
                coros_ = std::exchange(other.coros_, {});
                set_state_ = std::exchange(other.set_state_, {});
                get_result_ = std::exchange(other.get_result_, {});
                return *this;
            }

            auto operator co_await() && noexcept {
                return awaiter{std::move(*this)};
            }

        private:
            coros coros_;
            set_counter_fn set_state_;
            get_result_fn get_result_;
        };

        template<typename... Types, typename Alloc>
        when_all_t(when_all_task<Types, Alloc>...) -> when_all_t<Types...>;

        template<typename Alloc, typename Awaitable>
        auto do_when_all_task(std::allocator_arg_t, const Alloc&, Awaitable awaitable) -> when_all_task<await_result_t<Awaitable>, Alloc> {
            co_return co_await std::move(awaitable);
        }

        template<typename Awaitable>
        auto do_when_all_task(Awaitable awaitable) {
            return (do_when_all_task)(std::allocator_arg, std::allocator<void>{}, std::move(awaitable));
        }

        struct when_all_fn {
            template<awaitable_value... Awaitables> requires (sizeof...(Awaitables) > 0)
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST {
                return when_all_t{(do_when_all_task)(std::forward<Awaitables>(awaitables))...};
            }

            template<typename Alloc, awaitable_value... Awaitables> requires (sizeof...(Awaitables) > 0)
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (std::allocator_arg_t, const Alloc& alloc, Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST {
                return when_all_t{(do_when_all_task)(std::allocator_arg, alloc, std::forward<Awaitables>(awaitables)...)};
            }
        };

        template<typename T, typename Alloc>
        struct sync_wait_promise : promise_return_control<T>, promise_alloc_control<Alloc> {
            sync_wait_promise() = default;

            auto get_return_object() noexcept -> basic_task<sync_wait_promise> {
                return {std::coroutine_handle<sync_wait_promise>::from_promise(*this)};
            }

            auto unhandled_stopped() noexcept -> std::coroutine_handle<> {
                return complete();
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() noexcept -> task_final_awaiter {
                return {complete()};
            }

            auto complete() noexcept -> std::coroutine_handle<> {
                finished_ = 1;
                finished_.notify_all();
                return continuation_;
            }

            std::atomic_unsigned_lock_free finished_{0};
            std::coroutine_handle<> continuation_;
        };

        template<typename T, typename Alloc = void>
        using sync_wait_task = basic_task<sync_wait_promise<T, Alloc>>;

        template<typename T, typename Alloc>
        auto operator co_await(sync_wait_task<T, Alloc>& sync_) {
            struct awaiter {
                static auto await_ready() noexcept -> bool {
                    return false;
                }

                auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> std::coroutine_handle<> {
                    coro_.promise().continuation_ = this_coro;
                    return coro_;
                }

                static auto await_resume() noexcept -> void {}

                std::coroutine_handle<typename sync_wait_task<T, Alloc>::promise_type> coro_;
            };
            return awaiter{sync_.handle};
        }

        struct fire_and_forget {
            struct promise_type {
                static auto get_return_object() noexcept -> fire_and_forget {
                    return {};
                }

                static auto initial_suspend() noexcept -> std::suspend_never {
                    return {};
                }

                static auto final_suspend() noexcept -> std::suspend_never {
                    return {};
                }

                static auto return_void() noexcept -> void {}

                [[noreturn]]
                static auto unhandled_exception() noexcept -> void {
                    std::terminate();
                }

                auto unhandled_stopped() noexcept -> std::coroutine_handle<> {
                    return std::coroutine_handle<promise_type>::from_promise(*this);
                }
            };
        };

        template<typename Awaitable>
        auto do_sync_wait_task(Awaitable&& awaitable) -> sync_wait_task<await_result_t<Awaitable>> {
            co_return co_await std::forward<Awaitable>(awaitable);
        }

        struct sync_wait_fn {
            template<awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST -> optional_t<await_result_t<Awaitable>> {
                using ResultType = await_result_t<Awaitable>;
                auto sync_ = (do_sync_wait_task)(std::forward<Awaitable>(awt));
                [&]() -> fire_and_forget {
                    co_await sync_;
                }();
                sync_.handle.promise().finished_.wait(0);
                auto maybe_result = sync_.try_get_result();
                if (not maybe_result) return {};
                if constexpr (std::is_void_v<ResultType>) {
                    return optional_t<void>{std::in_place};
                }
                else {
                    return optional_t<ResultType>{
                        std::in_place,
                        std::forward<ResultType>(*maybe_result)
                    };
                }
            }
        };

        struct split_fn {
            template<awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator() (Awaitable awt) COIO_STATIC_CALL_OP_CONST -> shared_task<await_result_t<Awaitable>> {
                co_return co_await std::move(awt);
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

                template<typename Promise>
                auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> bool {
                    coro_ = this_coro;
                    scope_.list_.push(*this);
                    if constexpr (stoppable_promise<Promise>) {
                        stopped_callback_ = &detail::stop_stoppable_coroutine_<Promise>;
                    }
                    return scope_.ref_count_.fetch_sub(1, std::memory_order_acq_rel) > 1;
                }

                auto await_resume() -> void {
                    if (scope_.stop_source_.stop_requested()) {
                        stopped_callback_(coro_).resume();
                    }
                }

            private:
                async_scope& scope_;
                std::coroutine_handle<> coro_;
                detail::unhandled_stopped_fn stopped_callback_ = &detail::default_unhandled_stopped_;
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
        auto spawn(Awaitable awt) -> void {
            [](std::decay_t<Awaitable> awt_, retain_ptr<async_scope>) -> detail::fire_and_forget {
                void(co_await std::move(awt_));
            }(std::move(awt), retain_ptr{this});
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
        std::stop_source stop_source_;
        detail::intrusive_stack<join_sender::awaiter> list_{&join_sender::awaiter::next_};
    };

    inline constexpr detail::when_all_fn                 when_all{};
    inline constexpr detail::sync_wait_fn                sync_wait{};
    inline constexpr detail::then_fn                     then{};
    inline constexpr detail::just_fn                     just{};
    inline constexpr detail::just_error_fn               just_error{};
    inline constexpr detail::just_stopped_fn             just_stopped{};
    inline constexpr detail::split_fn                    split{};
}
