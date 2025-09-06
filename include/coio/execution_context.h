#pragma once
#include <atomic>
#include <chrono>
#include <coroutine>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <utility>
#include <vector>
#include "config.h"
#include "task.h"
#include "schedulers.h"

namespace coio {
    class execution_context {
    public:
        class schedule_sender;
        class sleep_sender;

        class scheduler {
            friend execution_context;
        public:
#ifdef COIO_ENABLE_SENDERS
            using scheduler_concept = detail::scheduler_tag;
#endif
        public:
            explicit scheduler(execution_context& ctx) noexcept : ctx_(&ctx) {}

            auto now() const noexcept -> std::chrono::steady_clock::time_point {
                return std::chrono::steady_clock::now();
            }

            COIO_ALWAYS_INLINE auto schedule() const noexcept -> schedule_sender {
                return schedule_sender{*ctx_};
            }

            template<typename Rep, typename Period>
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto schedule_after(std::chrono::duration<Rep, Period> duration) const noexcept -> sleep_sender {
                return sleep_sender{
                    *ctx_,
                    sleep_sender::clock_type::now() + std::chrono::duration_cast<sleep_sender::duration_type>(duration)
                };
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto schedule_at(std::chrono::steady_clock::time_point deadline) const noexcept -> sleep_sender {
                return sleep_sender{
                    *ctx_,
                    deadline
                };
            }

        public:
            friend auto operator== (const scheduler& lhs, const scheduler& rhs) -> bool = default;

        private:
            execution_context* ctx_;
        };

#ifdef COIO_ENABLE_SENDERS
        struct env {
            template<typename T>
            auto query(const execution::get_completion_scheduler_t<T>&) const noexcept -> scheduler {
                return scheduler{ctx_};
            }

            execution_context& ctx_; // NOLINT(*-avoid-const-or-ref-data-members)
        };
#endif

        class node {
            friend execution_context;
        public:
            node(execution_context& context) noexcept : context_(context) {}

            node(const node&) = delete;

            auto operator= (const node&) -> node& = delete;

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
            node* next_{};
        };

        class schedule_sender {
            friend execution_context;
        public:
            explicit schedule_sender(execution_context& context) noexcept : ctx_(&context) {}

#ifdef COIO_ENABLE_SENDERS
            auto get_env() const noexcept -> env {
                return {*ctx_};
            }
#endif
            auto operator co_await() const noexcept {
                struct awaiter : node {
                    using node::node;

                    static auto await_ready() noexcept -> bool {
                        return false;
                    }

                    auto await_suspend(std::coroutine_handle<> this_coro) -> bool {
                        coro_ = this_coro;
                        return post();
                    }

                    static auto await_resume() noexcept -> void {}
                };

                return awaiter{*ctx_};
            }

        private:
            execution_context* ctx_;
        };

        class sleep_sender {
            friend execution_context;
        public:
            using clock_type = std::chrono::steady_clock;
            using duration_type = clock_type::duration;
            using time_point_type = clock_type::time_point;

            struct awaiter : node {
                friend sleep_sender;
            private:
                awaiter(execution_context& context, time_point_type deadline) noexcept:
                    node(context), deadline(deadline) {}

            public:
                auto await_ready() noexcept -> bool {
                    context_.work_started();
                    return deadline <= clock_type::now();
                }

                auto await_suspend(std::coroutine_handle<> this_coro) -> void {
                    coro_ = this_coro;
                    std::scoped_lock _{context_.timer_queue_mtx_};
                    context_.timer_queue_.push(this);
                }

                auto await_resume() noexcept -> void {
                    context_.work_finished();
                }

                time_point_type deadline;
            };

        public:
            sleep_sender(execution_context& context, time_point_type deadline) noexcept : ctx_(&context), deadline_(deadline) {}

            sleep_sender(const sleep_sender&) = delete;

            sleep_sender(sleep_sender&& other) noexcept : ctx_(std::exchange(other.ctx_, {})), deadline_(std::exchange(other.deadline_, {})) {};

            ~sleep_sender() {
                if (ctx_) ctx_->work_finished();
            }

            auto operator= (const sleep_sender&) -> sleep_sender& = delete;

            auto operator= (sleep_sender&& other) noexcept -> sleep_sender& {
                ctx_ = std::exchange(other.ctx_, {});
                deadline_ = std::exchange(other.deadline_, {});
                return *this;
            }
#ifdef COIO_ENABLE_SENDERS
            auto get_env() const noexcept -> env {
                return {*ctx_};
            }
#endif
            auto operator co_await() && noexcept {
                COIO_ASSERT(ctx_ != nullptr);
                return awaiter{*std::exchange(ctx_, {}), std::exchange(deadline_, {})};
            }

        private:
            execution_context* ctx_;
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
            auto operator() (sleep_sender::awaiter* lhs, sleep_sender::awaiter* rhs) COIO_STATIC_CALL_OP_CONST noexcept -> bool {
                return rhs->deadline < lhs->deadline;
            }
        };

        using timer_queue = std::priority_queue<sleep_sender::awaiter*, std::vector<sleep_sender::awaiter*>, timer_compare>;

    public:
        execution_context() = default;

        execution_context(const execution_context&) = delete;

        auto operator= (const execution_context&) -> execution_context& = delete;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_scheduler() noexcept -> scheduler {
            return scheduler{*this};
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto schedule() noexcept -> schedule_sender {
            return schedule_sender{*this};
        }

        template<typename Rep, typename Period>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto schedule_after(std::chrono::duration<Rep, Period> duration) noexcept -> sleep_sender {
            return sleep_sender{
                *this,
                sleep_sender::clock_type::now() + std::chrono::duration_cast<sleep_sender::duration_type>(duration)
            };
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto schedule_at(sleep_sender::time_point_type deadline) noexcept -> sleep_sender {
            return sleep_sender{*this, deadline};
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

        COIO_ALWAYS_INLINE auto poll() -> std::size_t {
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
                if (sleep_op->deadline <= std::chrono::steady_clock::now()) {
                    timer_queue_.pop();
                    lock.unlock();
                    sleep_op->post();
                }
                else break;
            }
        }

        COIO_ALWAYS_INLINE auto request_stop() noexcept -> bool {
            return stop_source_.request_stop();
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto stop_requested() const noexcept -> bool {
            return stop_source_.stop_requested();
        }

        COIO_ALWAYS_INLINE auto work_started() noexcept -> void {
            ++work_count_;
        }

        COIO_ALWAYS_INLINE auto work_finished() noexcept -> void {
            --work_count_;
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto work_count() noexcept -> std::size_t {
            return work_count_;
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto make_work_guard() noexcept -> work_guard {
            return work_guard{*this};
        }

    private:
        std::stop_source stop_source_;
        std::mutex timer_queue_mtx_;
        timer_queue timer_queue_;
        std::mutex op_queue_mtx_;
        node* op_queue_head_{};
        node* op_queue_tail_{};
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