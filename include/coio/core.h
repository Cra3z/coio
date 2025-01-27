#pragma once
#include <atomic>
#include <coroutine>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <queue>
#include <ranges>
#include <stop_token>
#include <tuple>
#include <variant>
#include <vector>
#include <functional>
#include "config.h"
#include "concepts.h"
#include "detail/co_memory.h"
#include "detail/task_error.h"
#include "utils/retain_ptr.h"
#include "utils/type_traits.h"

namespace coio {
    template<awaiter T>
    using await_result_t = decltype(std::declval<T>().await_resume());

    template<awaitable T>
    struct awaitable_traits {
        using awaiter_type = typename detail::get_awaiter<T>::type;

        using await_result_type = await_result_t<awaiter_type>;
    };

    struct nothing {};

    template<typename T = void, typename Alloc = void>
    class [[nodiscard]] task;

    template<typename T = void, typename Alloc = void>
    class [[nodiscard]] shared_task;

    namespace detail {
        class queued_execution_context;
    }

    class steady_timer {
    public:
        using clock_type = std::chrono::steady_clock;
        using duration = clock_type::duration;
        using time_point = clock_type::time_point;

    public:
        struct sleep_operation;

    public:
        explicit steady_timer(detail::queued_execution_context& pool) noexcept : pool_(&pool) {}

        steady_timer(detail::queued_execution_context& pool, std::chrono::steady_clock::time_point timeout) noexcept : pool_(&pool), deadline_(timeout) {}

        steady_timer(detail::queued_execution_context& pool, std::chrono::steady_clock::duration duration) noexcept : steady_timer(pool, clock_type::now() + duration) {}

        steady_timer(steady_timer&& other) noexcept : pool_(other.pool_), deadline_(std::exchange(other.deadline_, {})) {}

        auto operator= (steady_timer other) noexcept -> steady_timer& {
            std::swap(other.deadline_, deadline_);
            std::swap(other.pool_, pool_);
            return *this;
        }

        [[nodiscard]]
        auto async_wait() noexcept -> sleep_operation;

        [[nodiscard]]
        auto context() const noexcept -> detail::queued_execution_context& {
            return *pool_;
        }
    private:
        detail::queued_execution_context* pool_;
        time_point deadline_;
    };

    namespace detail {
        struct timer_compare {
            COIO_STATIC_CALL_OP auto operator() (steady_timer::sleep_operation* lhs, steady_timer::sleep_operation* rhs) COIO_STATIC_CALL_OP_CONST noexcept -> bool ;
        };

        class queued_execution_context {
            friend steady_timer::sleep_operation;
        public:
            class async_operation_base {
                friend queued_execution_context;
            public:
                async_operation_base(queued_execution_context& pool_) noexcept : context_(pool_) {}

                async_operation_base(const async_operation_base&) = delete;

                auto operator= (const async_operation_base&) -> async_operation_base& = delete;

                auto post() -> bool {
                    next_ = nullptr;
                    if (context_.stop_requested()) return false;
                    std::unique_lock lock{context_.op_queue_mtx_};
                    if (auto old_tail = std::exchange(context_.op_queue_tail_, this)) old_tail->next_ = this;
                    if (context_.op_queue_head_ == nullptr) context_.op_queue_head_ = this;
                    context_.op_queue_mtx_.unlock();
                    context_.op_queue_cv_.notify_one();
                    return true;
                }

            protected:
                queued_execution_context& context_;
                std::coroutine_handle<> coro_;
                async_operation_base* next_{};
            };

            class schedule_operation : public async_operation_base {
                friend queued_execution_context;
            protected:
                using async_operation_base::async_operation_base;

            public:
                static auto await_ready() noexcept -> bool {
                    return false;
                }

                auto await_suspend(std::coroutine_handle<> this_coro) -> bool {
                    coro_ = this_coro;
                    return post();
                }

                static auto await_resume() noexcept -> void {}
            };

            class work_guard {
            public:
                explicit work_guard(queued_execution_context& ctx) noexcept : pool_(&ctx) {
                    pool_->work_started();
                }

                work_guard(const work_guard& other) noexcept : pool_(other.pool_) {
                    if (pool_) pool_->work_started();
                }

                work_guard(work_guard&& other) noexcept : pool_(std::exchange(other.pool_, {})) {}

                ~work_guard() {
                    if (pool_) pool_->work_finished();
                }

                auto operator= (work_guard other) noexcept -> work_guard& {
                    std::swap(pool_, other.pool_);
                    return *this;
                }
            private:
                queued_execution_context* pool_;
            };

        public:
            queued_execution_context() = default;

            queued_execution_context(const queued_execution_context&) = delete;

            auto operator= (const queued_execution_context&) -> queued_execution_context& = delete;

            [[nodiscard]]
            auto schedule() noexcept -> schedule_operation {
                return *this;
            }

            template<typename Fn, typename... Args> requires std::invocable<Fn, Args...> && std::movable<std::invoke_result_t<Fn, Args...>>
            [[nodiscard]]
            auto submit(Fn fn, Args... args) -> task<std::invoke_result_t<Fn, Args...>> {
                co_await schedule();
                co_return std::invoke(fn, std::move(args)...);
            }

            auto poll_one() -> bool {
                std::unique_lock lock{op_queue_mtx_};
                if (stop_requested() or op_queue_head_ == nullptr) return false;
                if (op_queue_head_ == op_queue_tail_) op_queue_tail_ = nullptr;
                auto op = std::exchange(op_queue_head_, op_queue_head_->next_);
                lock.unlock();
                op->coro_.resume();
                return true;
            }

            auto poll() -> std::size_t {
                std::size_t count = 0;
                while (poll_one()) ++count;
                return count;
            }

            auto make_timeout_timers_ready() -> void;

            auto request_stop() noexcept -> bool {
                return stop_source_.request_stop();
            }

            [[nodiscard]]
            auto stop_requested() const noexcept -> bool {
                return stop_source_.stop_requested();
            }

            auto work_started() noexcept ->void {
                ++work_count_;
            }

            auto work_finished() noexcept ->void {
                --work_count_;
            }

            [[nodiscard]]
            auto work_count() noexcept -> std::size_t {
                return work_count_;
            }

            [[nodiscard]]
            auto make_work_guard() noexcept -> work_guard {
                return work_guard{*this};
            }

        private:
            std::stop_source stop_source_;
            std::mutex timer_queue_mtx_;
            std::priority_queue<steady_timer::sleep_operation*, std::vector<steady_timer::sleep_operation*>, timer_compare> timer_queue_;
            std::condition_variable op_queue_cv_;
            std::mutex op_queue_mtx_;
            async_operation_base* op_queue_head_{};
            async_operation_base* op_queue_tail_{};
            std::atomic<std::size_t> work_count_{0};
        };
    }

    struct steady_timer::sleep_operation : detail::queued_execution_context::async_operation_base {
        sleep_operation(detail::queued_execution_context& pool, time_point deadline) noexcept : async_operation_base(pool), deadline_(deadline) {
            context_.work_started();
        }

        ~sleep_operation() {
            context_.work_finished();
        }

        auto await_ready() noexcept -> bool {
            return deadline_ <= clock_type::now();
        }

        auto await_suspend(std::coroutine_handle<> this_coro) -> void {
            coro_ = this_coro;
            std::scoped_lock _{context_.timer_queue_mtx_};
            context_.timer_queue_.push(this);
        }

        static auto await_resume() noexcept -> void {}

        time_point deadline_;
    };

    inline auto steady_timer::async_wait() noexcept -> sleep_operation {
        return {*pool_, deadline_};
    }

    inline auto detail::timer_compare::operator()(steady_timer::sleep_operation* lhs, steady_timer::sleep_operation* rhs) const noexcept -> bool {
        return rhs->deadline_ < lhs->deadline_;
    }

    inline auto detail::queued_execution_context::make_timeout_timers_ready() -> void {
        while (true) {
            if (not timer_queue_mtx_.try_lock()) break;
            std::unique_lock lock{timer_queue_mtx_, std::adopt_lock};
            if (timer_queue_.empty()) break;
            auto sleep_op = timer_queue_.top();
            if (sleep_op->deadline_ <= std::chrono::steady_clock::now()) {
                timer_queue_.pop();
                lock.unlock();
                sleep_op->post();
            }
            else break;
        }
    }

    class io_context : public detail::queued_execution_context {
    public:
        struct impl;
    public:
        io_context();

        io_context(const io_context&) = delete;

        ~io_context();

        auto operator= (const io_context&) -> io_context& = delete;

        auto run() -> void;

    private:
        std::unique_ptr<impl> pimpl_;
    };


    namespace detail {
        template<typename T>
        using awaitable_await_result_t = typename awaitable_traits<T>::await_result_type;

        template<typename T>
        using void_to_nothing = std::conditional_t<std::is_void_v<T>, nothing, T>;

        template<typename T>
        using awaitable_non_void_await_result_t = void_to_nothing<awaitable_await_result_t<T>>;

        template<typename>
        struct task_traits;

        template<typename T, typename Alloc>
        struct task_traits<task<T, Alloc>> {
            using elem_t = T;
            using alloc_t = Alloc;
            using promise_t = typename std::coroutine_traits<task<T, Alloc>>::promise_type;
        };

        template<typename T, typename Alloc>
        struct task_traits<shared_task<T, Alloc>> {
            using elem_t = T;
            using alloc_t = Alloc;
            using promise_t = typename std::coroutine_traits<shared_task<T, Alloc>>::promise_type;
        };

        template<typename T>
        using task_elem_t = typename task_traits<T>::elem_t;

        template<typename T>
        using task_alloc_t = typename task_traits<T>::alloc_t;

        template<typename T>
        using task_promise_t = typename task_traits<T>::promise_t;

        template<typename T>
        class promise_return_control {
        public:

            auto return_value(T value) noexcept(std::is_nothrow_constructible_v<wrap_ref_t<T>, T>) ->void {
                result_.template emplace<1>(static_cast<T&&>(value));
            }

            auto unhandled_exception() noexcept ->void {
                result_.template emplace<2>(std::current_exception());
            }

            auto get_result() ->T& {
                COIO_ASSERT(result_.index() > 0);
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
                return *std::get_if<1>(&result_);
            }

        private:
            std::variant<std::monostate, wrap_ref_t<T>, std::exception_ptr> result_;
        };

        template<>
        class promise_return_control<void> {
        public:

            auto return_void() noexcept ->void {
                result_.emplace<1>();
            }

            auto unhandled_exception() noexcept ->void {
                result_.emplace<2>(std::current_exception());
            }

            auto get_result() ->void {
                COIO_ASSERT(result_.index() > 0);
                if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
            }

        private:
            std::variant<std::monostate, nothing, std::exception_ptr> result_;
        };

        template<typename TaskType>
        struct task_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) const noexcept -> std::coroutine_handle<> {
                COIO_DCHECK(coro != nullptr);
                coro.promise().prev_coro_ = this_coro;
                return coro;
            }

            auto await_resume() const ->task_elem_t<TaskType> {
                auto& promise = coro.promise();
                return static_cast<std::add_rvalue_reference_t<task_elem_t<TaskType>>>(promise.get_result());
            }

            std::coroutine_handle<task_promise_t<TaskType>> coro;
        };

        struct task_final_awaiter {
            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<>) const noexcept -> std::coroutine_handle<> {
                if (prev_coro) return prev_coro;
                return std::noop_coroutine();
            }

            static auto await_resume() noexcept ->void {}

            std::coroutine_handle<> prev_coro;
        };

        struct shared_task_node {
            shared_task_node() = default;

            shared_task_node(const shared_task_node&) = delete;

            auto operator=(const shared_task_node&) -> shared_task_node& = delete;

            std::coroutine_handle<> waiter;
            shared_task_node* next = nullptr;
        };

        template<typename SharedTaskType>
        struct shared_task_awaiter : shared_task_node {

            shared_task_awaiter(std::coroutine_handle<task_promise_t<SharedTaskType>> coro) noexcept : coro(coro) {}

            auto await_ready() noexcept -> bool {
                COIO_DCHECK(coro != nullptr);
                return coro.promise().step_ == 2;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> std::coroutine_handle<> {
                waiter = this_coro;
                next = coro.promise().head_.exchange(this, std::memory_order_acq_rel);
                if (int zero = 0; coro.promise().step_.compare_exchange_strong(zero, 1)) {
                    return coro;
                }
                return std::noop_coroutine();
            }

            auto await_resume() -> add_const_lvalue_ref_t<task_elem_t<SharedTaskType>> {
                return coro.promise().get_result();
            }

            std::coroutine_handle<task_promise_t<SharedTaskType>> coro;
        };

        struct shared_task_final_awaiter {
            static auto await_ready() noexcept ->bool {
                return false;
            }

            template<typename SharedTaskPromise>
            static auto await_suspend(std::coroutine_handle<SharedTaskPromise> this_coro) noexcept -> void {
                shared_task_node* awaiter_ = this_coro.promise().head_.load(std::memory_order_acquire);
                while (awaiter_) {
                    auto next = awaiter_->next;
                    awaiter_->waiter.resume();
                    awaiter_ = next;
                }
            }

            static auto await_resume() noexcept -> void {}
        };

        template<typename TaskType>
        struct task_promise : promise_return_control<task_elem_t<TaskType>>, promise_alloc_control<task_alloc_t<TaskType>> {
            friend task_awaiter<TaskType>;

            task_promise() = default;

            auto get_return_object() noexcept ->TaskType {
                return std::coroutine_handle<task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() const noexcept -> task_final_awaiter {
                return {prev_coro_};
            }

        private:
            std::coroutine_handle<> prev_coro_;
        };

        template<typename SharedTaskType>
        struct shared_task_promise : promise_return_control<task_elem_t<SharedTaskType>>, promise_alloc_control<task_alloc_t<SharedTaskType>> {
            friend shared_task_final_awaiter;
            friend shared_task_awaiter<SharedTaskType>;

            using handle_type = std::coroutine_handle<shared_task_promise>;

            shared_task_promise() = default;

            auto get_return_object() noexcept -> SharedTaskType {
                return std::coroutine_handle<shared_task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() noexcept -> shared_task_final_awaiter {
                step_ = 2;
                step_.notify_all();
                return {};
            }

            auto retain() noexcept -> void {
                ref_count_.fetch_add(1, std::memory_order_relaxed);
            }

            auto lose() noexcept -> void {
                if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    auto handle = handle_type::from_promise(*this);
                    handle.destroy();
                }
            }

        private:
            std::atomic<shared_task_node*> head_{nullptr};
            std::atomic<std::size_t> ref_count_{0};
            std::atomic_unsigned_lock_free step_{0}; // 0: not started, 1: started, 2: finished
        };

        struct to_awaiter_fn {
            template<awaitable Awaitable>
            COIO_STATIC_CALL_OP awaiter decltype(auto) operator() (Awaitable&& awt) COIO_STATIC_CALL_OP_CONST noexcept(get_awaiter<Awaitable>::nothrow) {
                if constexpr (requires { operator co_await(std::declval<Awaitable>()); }) {
                    return operator co_await(std::forward<Awaitable>(awt));
                }
                else if constexpr (requires {std::declval<Awaitable>().operator co_await(); }) {
                    return std::forward<Awaitable>(awt).operator co_await();
                }
                else return std::forward<Awaitable>(awt);
            }
        };

    }

    template<typename T, typename Alloc>
    class task {
        static_assert(
            std::same_as<T, void> or
            (unqualified_object<T> and std::move_constructible<T>) or
            std::is_lvalue_reference_v<T>,
            "type `T` shall be `void` or a move-constructible object-type or a lvaue-reference type."
        );

        static_assert(
            detail::valid_coroutine_alloctor_<Alloc>,
            "type `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
        );

        friend detail::task_promise<task>;

    public:

        using promise_type = detail::task_promise<task>;

    private:

        task(std::coroutine_handle<promise_type> coro) noexcept : coro_{coro} {}

    public:

        task() = default;

        task(const task&) = delete;

        task(task&& other) noexcept : coro_{std::exchange(other.coro_, {})} {}

        ~task() {
            if (coro_) coro_.destroy();
        }

        auto operator= (const task&) -> task& = delete;

        auto operator= (task&& other) noexcept -> task& {
            task(std::move(other)).swap(*this);
            return *this;
        }

        explicit operator bool() const noexcept {
            return coro_ != nullptr;
        }

        auto operator co_await() const -> detail::task_awaiter<task> {
            if (coro_ == nullptr) [[unlikely]] throw task_error{task_errc::no_state};
            if (coro_.done()) [[unlikely]] throw task_error{task_errc::already_retrieved};
            return {coro_};
        }

        auto swap(task& other) noexcept ->void {
            std::swap(coro_, other.coro_);
        }

        friend auto swap(task& lhs, task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

        [[nodiscard]]
        auto ready() const noexcept ->bool {
            COIO_DCHECK(coro_ != nullptr);
            return coro_.done();
        }

        auto reset() noexcept ->void {
            task{}.swap(*this);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
    };

    template<typename T, typename Alloc>
    class shared_task {
        static_assert(
            std::same_as<T, void> or
            unqualified_object<T> or
            std::is_lvalue_reference_v<T>,
            "type `T` shall be `void` or an object-type or a lvaue-reference type."
        );
        static_assert(
            detail::valid_coroutine_alloctor_<Alloc>,
            "typename `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
        );

        friend detail::shared_task_promise<shared_task>;

    public:

        using promise_type = detail::shared_task_promise<shared_task>;

    private:

        shared_task(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro), retain_(coro ? &coro.promise() : nullptr) {} // NOLINT

    public:

        shared_task() noexcept = default;

        shared_task(const shared_task& other) = default;

        shared_task(shared_task&& other) noexcept : coro_(std::exchange(other.coro_, {})), retain_(std::move(other.retain_)) {}

        ~shared_task() = default;

        auto operator= (shared_task other) noexcept ->shared_task& {
            shared_task{std::move(other)}.swap(*this);
            return *this;
        }

        explicit operator bool() const noexcept {
            return coro_ != nullptr;
        }

        auto operator co_await() const ->detail::shared_task_awaiter<shared_task> {
            if (coro_ == nullptr) [[unlikely]] throw task_error{task_errc::no_state};
            return {coro_};
        }

        [[nodiscard]]
        auto ready() const noexcept ->bool {
            COIO_DCHECK(coro_ != nullptr);
            return coro_.promise().step_ == 2;
        }

        auto swap(shared_task& other) noexcept ->void {
            std::ranges::swap(coro_, other.coro_);
            std::ranges::swap(retain_, other.retain_);
        }

        friend auto swap(shared_task& lhs, shared_task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

        auto reset() noexcept ->void {
            shared_task{}.swap(*this);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
        retain_ptr<promise_type> retain_;
    };

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
            explicit when_all_counter(std::size_t count) noexcept : count(count) {
                COIO_DCHECK(count > 0);
            }

            [[nodiscard]]
            auto decrease() noexcept ->std::coroutine_handle<> {
                if (--count == 0) return prev_coro;
                return std::noop_coroutine();
            }

            std::atomic<std::size_t> count;
            std::coroutine_handle<> prev_coro;
        };

        template<typename T, typename OnFinalSuspend>
        struct task_wrapper {
            static_assert(std::default_initializable<OnFinalSuspend> and std::is_nothrow_invocable_r_v<std::coroutine_handle<>, OnFinalSuspend>);

            struct promise_type : promise_return_control<T> {
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

            auto swap(task_wrapper& other) noexcept ->void {
                std::swap(coro_, other.coro_);
            }

            friend auto swap(task_wrapper& lhs, task_wrapper& rhs) noexcept ->void {
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
            COIO_STATIC_CALL_OP auto operator() () COIO_STATIC_CALL_OP_CONST noexcept ->std::coroutine_handle<> {
                COIO_DCHECK(counter_ != nullptr);
                return counter_->decrease();
            }
            when_all_counter* counter_ = nullptr;
        };

        template<typename T>
        using when_all_task = task_wrapper<T, on_when_all_task_final_suspend_fn>;

        template<typename>
        class when_all_ready_awaiter;

        template<elements_move_insertable_range WhenAllTasks> requires std::move_constructible<WhenAllTasks> and specialization_of<std::ranges::range_value_t<WhenAllTasks>, task_wrapper>
        class when_all_ready_awaiter<WhenAllTasks> {
        public:
            when_all_ready_awaiter(WhenAllTasks when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)), counter_(std::make_unique<when_all_counter>(std::ranges::distance(when_all_tasks_))) {}

            auto await_ready() const noexcept -> bool {
                return bool(counter_->prev_coro);
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
                counter_->prev_coro = this_coro;
                for (auto& task : when_all_tasks_) {
                    task.coro_.promise().on_final_suspend_.counter_ = counter_.get();
                    task.coro_.resume();
                };
            }

            [[nodiscard]]
            auto await_resume() noexcept(std::is_nothrow_move_constructible_v<WhenAllTasks>) -> WhenAllTasks {
                return std::move(when_all_tasks_);
            }

        private:
            WhenAllTasks when_all_tasks_;
            std::unique_ptr<when_all_counter> counter_;
        };

        template<typename... Types>
        class when_all_ready_awaiter<std::tuple<when_all_task<Types>...>> {
        public:

            when_all_ready_awaiter(std::tuple<when_all_task<Types>...> when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)) {}

            auto await_ready() const noexcept -> bool {
                return bool(counter_->prev_coro);
            }

            auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
                counter_->prev_coro = this_coro;
                [this]<std::size_t... I>(std::index_sequence<I...>) {
                    (..., (std::get<I>(when_all_tasks_).coro_.promise().on_final_suspend_.counter_ = counter_.get()));
                    (..., std::get<I>(when_all_tasks_).coro_.resume());
                }(std::make_index_sequence<sizeof...(Types)>{});
            }

            [[nodiscard]]
            auto await_resume() noexcept(std::is_nothrow_move_constructible_v<std::tuple<when_all_task<Types>...>>) -> std::tuple<when_all_task<Types>...> {
                return std::move(when_all_tasks_);
            }

        private:
            std::tuple<when_all_task<Types>...> when_all_tasks_;
            std::unique_ptr<when_all_counter> counter_ = std::make_unique<when_all_counter>(sizeof...(Types));
        };

        template<typename... Types>
        when_all_ready_awaiter(std::tuple<when_all_task<Types>...>) -> when_all_ready_awaiter<std::tuple<when_all_task<Types>...>>;

        inline constexpr auto do_when_all_task = []<typename Awaitable>(Awaitable awaitable) COIO_STATIC_CALL_OP ->when_all_task<awaitable_await_result_t<Awaitable>> {
            if constexpr (std::is_void_v<awaitable_await_result_t<Awaitable>>) {
                co_await awaitable;
                co_return;
            }
            else co_return co_await awaitable;
        };

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
                        do_when_all_task(std::forward<Awaitables>(awaitables))...
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
                    void(result.push_back(do_when_all_task(*(first++))));
                }
                return when_all_ready_awaiter<when_all_task_container_t>{std::move(result)};
            }

            template<borrowed_forward_range TaskRange> requires awaitable<std::ranges::range_value_t<TaskRange>> and std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng) COIO_STATIC_CALL_OP_CONST {
                return (*this)(std::ranges::begin(rng), std::ranges::end(rng));
            }

        };

        struct when_all_fn {
            template<awaitable... Awaitables>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
                return transform_awaiter{
                    when_all_ready_fn{}(std::forward<Awaitables>(awaitables)...),
                    []<typename... Types>(std::tuple<when_all_task<Types>...> when_all_tasks_) {
                        return [&when_all_tasks_]<std::size_t... I>(std::index_sequence<I...>) {
                            return std::tuple<void_to_nothing<Types>...>{std::get<I>(when_all_tasks_).get_non_void_result()...};
                        }(std::make_index_sequence<sizeof...(Types)>{});
                    }
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

        template<typename T>
        using sync_wait_task = task_wrapper<T, on_sync_wait_final_suspend_fn>;

        inline constexpr auto do_sync_wait_task = []<typename Awaitable>(Awaitable awaitable) COIO_STATIC_CALL_OP ->sync_wait_task<awaitable_await_result_t<Awaitable>> {
            if constexpr (std::is_void_v<awaitable_await_result_t<Awaitable>>) {
                co_await awaitable;
                co_return;
            }
            else co_return co_await awaitable;
        };

        struct sync_wait_fn {
            COIO_STATIC_CALL_OP auto operator() (awaitable auto&& awt) COIO_STATIC_CALL_OP_CONST ->awaitable_await_result_t<decltype(awt)> {
                auto sync_ = do_sync_wait_task(std::forward<decltype(awt)>(awt));
                sync_.coro_.resume();
                sync_.coro_.promise().on_final_suspend_.finished_.wait(0);
                return static_cast<std::add_rvalue_reference_t<awaitable_await_result_t<decltype(awt)>>>(sync_.get_result());
            }
        };
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
        };
    };

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
