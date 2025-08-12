#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <utility>
#include <vector>
#include "config.h"
#include "task.h"

namespace coio {
    class execution_context {
    public:
        class async_operation {
            friend execution_context;
        public:
            async_operation(execution_context& context) noexcept : context_(context) {}

            async_operation(const async_operation&) = delete;

            auto operator= (const async_operation&) -> async_operation& = delete;

            auto context() const noexcept -> execution_context& {
                return context_;
            }

            auto post() -> bool {
                next_ = nullptr;
                if (context_.stop_requested()) return false;
                std::unique_lock lock{context_.op_queue_mtx_};
                if (auto old_tail = std::exchange(context_.op_queue_tail_, this)) old_tail->next_ = this;
                if (context_.op_queue_head_ == nullptr) context_.op_queue_head_ = this;
                return true;
            }

        protected:
            execution_context& context_;
            std::coroutine_handle<> coro_;
            async_operation* next_{};
        };

        class schedule_operation : public async_operation {
            friend execution_context;
        protected:
            using async_operation::async_operation;

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

        class sleep_operation : public async_operation {
            friend execution_context;
        public:
            using clock_type = std::chrono::steady_clock;
            using duration_type = clock_type::duration;
            using time_point_type = clock_type::time_point;

        public:
            sleep_operation(execution_context& context, time_point_type deadline) noexcept :
                async_operation(context),
                deadline_(deadline)
            {
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

        private:
            time_point_type deadline_;
        };

        class work_guard {
        public:
            explicit work_guard(execution_context& context) noexcept : context_(&context) {
                context_->work_started();
            }

            work_guard(const work_guard& other) noexcept : context_(other.context_) {
                if (context_) context_->work_started();
            }

            work_guard(work_guard&& other) noexcept : context_(std::exchange(other.context_, {})) {}

            ~work_guard() {
                if (context_) context_->work_finished();
            }

            auto operator= (work_guard other) noexcept -> work_guard& {
                std::swap(context_, other.context_);
                return *this;
            }
        private:
            execution_context* context_;
        };

    private:
        struct timer_compare {
            COIO_STATIC_CALL_OP
            auto operator() (sleep_operation* lhs, sleep_operation* rhs) COIO_STATIC_CALL_OP_CONST noexcept -> bool {
                return rhs->deadline_ < lhs->deadline_;
            }
        };

        using timer_queue = std::priority_queue<sleep_operation*, std::vector<sleep_operation*>, timer_compare>;

    public:
        execution_context() = default;

        execution_context(const execution_context&) = delete;

        auto operator= (const execution_context&) -> execution_context& = delete;

        [[nodiscard]]
        auto schedule() noexcept -> schedule_operation {
            return *this;
        }

        template<typename Fn, typename... Args>
            requires std::invocable<Fn, Args...> && std::movable<std::invoke_result_t<Fn, Args...>>
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

        auto make_timeout_timers_ready() -> void {
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

        auto request_stop() noexcept -> bool {
            return stop_source_.request_stop();
        }

        [[nodiscard]]
        auto stop_requested() const noexcept -> bool {
            return stop_source_.stop_requested();
        }

        auto work_started() noexcept -> void {
            ++work_count_;
        }

        auto work_finished() noexcept -> void {
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
        timer_queue timer_queue_;
        std::mutex op_queue_mtx_;
        async_operation* op_queue_head_{};
        async_operation* op_queue_tail_{};
        std::atomic<std::size_t> work_count_{0};
    };


    class io_context : public execution_context {
    public:
        struct impl;

        io_context();

        io_context(const io_context&) = delete;

        ~io_context();

        auto operator= (const io_context&) -> io_context& = delete;

        auto run() -> void;

    private:
        std::unique_ptr<impl> pimpl_;
    };
}