// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
#pragma once
#include <atomic>
#include <chrono>
#include <coroutine>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <limits>
#include <queue>
#include <stop_token>
#include <utility>
#include <vector>
#include "detail/execution.h"
#include "detail/op_queue.h"
#include "utils/stop_token.h"

namespace coio {
    namespace detail {
        class conditional_mutex {
        public:
            explicit conditional_mutex(bool enabled) noexcept {
                if (enabled) mtx_.emplace();
            }

            auto lock() -> void {
                if (mtx_) mtx_->lock();
            }

            auto unlock() -> void {
                if (mtx_) mtx_->unlock();
            }

            [[nodiscard]]
            auto try_lock() noexcept -> bool {
                if (mtx_) return mtx_->try_lock();
                return true;
            }

        private:
            std::optional<std::mutex> mtx_;
        };

        template<typename Ctx>
        class run_loop_base {
            friend Ctx;
        public:
            class operation_base {
                friend Ctx;
                friend run_loop_base;
            public:
                using operation_state_concept = execution::operation_state_t;

            public:
                operation_base(Ctx& context) noexcept : context_(context) {
                    ++context.work_count_;
                }

                operation_base(const operation_base&) = delete;

                ~operation_base() {
                    --context_.work_count_;
                }

                auto operator= (const operation_base&) -> operation_base& = delete;

                virtual auto finish() -> void = 0;

            protected:
                Ctx& context_;
                operation_base* next_{};
            };

            struct env {
                auto query(execution::get_completion_scheduler_t<execution::set_value_t>) const noexcept {
                    return ctx_.get_scheduler();
                }

                auto query(get_allocator_t) const noexcept -> std::pmr::polymorphic_allocator<> {
                    return ctx_.get_allocator();
                }

                Ctx& ctx_; // NOLINT(*-avoid-const-or-ref-data-members)
            };

            class schedule_sender {
                friend Ctx;
            private:
                template<typename Receiver>
                struct op_state : operation_base {
                public:
                    op_state(Ctx& context, Receiver rcvr) noexcept : operation_base(context), rcvr_(std::move(rcvr)) {}

                    COIO_ALWAYS_INLINE auto start() & noexcept -> void try {
                        if (coio::get_stop_token(execution::env(rcvr_)).stop_requested()) {
                            execution::set_stopped(std::move(rcvr_));
                            return;
                        }
                        this->context_.op_queue_.enqueue(*this);
                        this->context_.interrupt();
                    }
                    catch (...) {
                        execution::set_error(std::move(rcvr_), std::current_exception());
                    }

                    auto finish() -> void override {
                        if (coio::get_stop_token(execution::env(rcvr_)).stop_requested()) {
                            execution::set_stopped(std::move(rcvr_));
                            return;
                        }
                        execution::set_value(std::move(rcvr_));
                    }

                private:
                    Receiver rcvr_;
                };

            public:
                using sender_concept = execution::sender_t;
                using completion_signatures = execution::completion_signatures<
                    execution::set_value_t(),
                    execution::set_error_t(std::exception_ptr),
                    execution::set_stopped_t()
                >;

            public:
                explicit schedule_sender(Ctx& context) noexcept : ctx_(&context) {}

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*ctx_};
                }

                template<execution::receiver_of<completion_signatures> Receiver>
                COIO_ALWAYS_INLINE auto connect(Receiver rcvr) && noexcept {
                    COIO_ASSERT(ctx_ != nullptr);
                    return op_state{*std::exchange(ctx_, {}), std::move(rcvr)};
                }

            private:
                Ctx* ctx_;
            };

            class sleep_sender {
                friend Ctx;
            public:
                using sender_concept = execution::sender_t;
                using completion_signatures = execution::completion_signatures<
                    execution::set_value_t(),
                    execution::set_error_t(std::exception_ptr),
                    execution::set_stopped_t()
                >;
                using clock_type = std::chrono::steady_clock;
                using duration_type = clock_type::duration;
                using time_point_type = clock_type::time_point;

                struct op_state_base : operation_base {
                    op_state_base(Ctx& context, time_point_type deadline) noexcept: operation_base(context), deadline(deadline) {}

                    time_point_type deadline;
                };

                template<typename Receiver>
                struct op_state : op_state_base {
                public:
                    op_state(Ctx& context, time_point_type deadline, Receiver rcvr) noexcept:
                        op_state_base(context, deadline), rcvr(std::move(rcvr)) {}

                    COIO_ALWAYS_INLINE auto start() & noexcept -> void try {
                        if (this->context_.timer_queue_.add(*this)) this->context_.interrupt();
                    }
                    catch (...) {
                        execution::set_error(std::move(rcvr), std::current_exception());
                    }

                    Receiver rcvr;
                };

            public:
                sleep_sender(Ctx& context, time_point_type deadline) noexcept : ctx_(&context), deadline_(deadline) {}

                sleep_sender(const sleep_sender&) = delete;

                sleep_sender(sleep_sender&& other) noexcept : ctx_(std::exchange(other.ctx_, {})), deadline_(std::exchange(other.deadline_, {})) {};

                ~sleep_sender() = default;

                auto operator= (const sleep_sender&) -> sleep_sender& = delete;

                auto operator= (sleep_sender&& other) noexcept -> sleep_sender& {
                    ctx_ = std::exchange(other.ctx_, {});
                    deadline_ = std::exchange(other.deadline_, {});
                    return *this;
                }

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*ctx_};
                }

                template<execution::receiver_of<completion_signatures> Receiver>
                COIO_ALWAYS_INLINE auto connect(Receiver rcvr) && noexcept {
                    COIO_ASSERT(ctx_ != nullptr);
                    return op_state{*std::exchange(ctx_, {}), std::exchange(deadline_, {}), std::move(rcvr)};
                }

            private:
                Ctx* ctx_;
                time_point_type deadline_;
            };

        private:
            class scheduler_base {
            public:
                using scheduler_concept = execution::scheduler_t;

            public:
                explicit scheduler_base(Ctx& ctx) noexcept : ctx_(&ctx) {}

                [[nodiscard]]
                COIO_ALWAYS_INLINE static auto now() noexcept -> std::chrono::steady_clock::time_point {
                    return std::chrono::steady_clock::now();
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto schedule() const noexcept -> schedule_sender {
                    return schedule_sender{*ctx_};
                }

                template<typename Rep, typename Period>
                [[nodiscard]]
                COIO_ALWAYS_INLINE auto schedule_after(std::chrono::duration<Rep, Period> duration) const noexcept -> sleep_sender {
                    return sleep_sender{
                        *ctx_,
                        now() + std::chrono::duration_cast<typename sleep_sender::duration_type>(duration)
                    };
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto schedule_at(std::chrono::steady_clock::time_point deadline) const noexcept -> sleep_sender {
                    return sleep_sender{
                        *ctx_,
                        deadline
                    };
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto context() const noexcept -> Ctx& {
                    COIO_ASSERT(ctx_ != nullptr);
                    return *ctx_;
                }

                friend auto operator== (const scheduler_base& lhs, const scheduler_base& rhs) -> bool = default;

            protected:
                Ctx* ctx_;
            };

            using timer_queue = detail::timer_queue<typename sleep_sender::op_state_base, [](const typename sleep_sender::op_state_base& op) noexcept {
                return op.deadline;
            }, std::pmr::polymorphic_allocator<>>;

            using op_queue = detail::op_queue<operation_base, &operation_base::next_>;

        private:
            run_loop_base() = default;

            explicit run_loop_base(std::pmr::memory_resource& memory_resource) noexcept : allocator_(&memory_resource) {}

        public:
            run_loop_base(const run_loop_base&) = delete;

            auto operator= (const run_loop_base&) -> run_loop_base& = delete;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_scheduler() noexcept {
                using scheduler_t = typename Ctx::scheduler;
                return scheduler_t{static_cast<Ctx&>(*this)};
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_allocator() const noexcept -> std::pmr::polymorphic_allocator<> {
                return allocator_;
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_stop_token() const noexcept -> inplace_stop_token {
                return stop_source_.get_token();
            }

            COIO_ALWAYS_INLINE auto request_stop() -> bool {
                auto self = static_cast<Ctx*>(this);
                const auto result = stop_source_.request_stop();
                if (result) self->interrupt();
                return result;
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto stop_requested() const noexcept -> bool {
                return stop_source_.stop_requested();
            }

            COIO_ALWAYS_INLINE auto work_started() noexcept -> void {
                ++work_count_;
            }

            COIO_ALWAYS_INLINE auto work_finished() noexcept -> void {
                if (--work_count_ == 0) {
                    auto self = static_cast<Ctx*>(this);
                    self->request_stop();
                }
            }

            auto poll_one() -> bool {
                auto self = static_cast<Ctx*>(this);
                if (work_count_ == 0) self->request_stop();
                return self->do_one(false);
            }

            auto poll() -> std::size_t {
                auto self = static_cast<Ctx*>(this);
                if (work_count_ == 0) self->request_stop();
                std::size_t count = 0;
                while (poll_one()) {
                    if (count < std::numeric_limits<std::size_t>::max()) ++count;
                }
                return count;
            }

            auto run_one() -> bool {
                auto self = static_cast<Ctx*>(this);
                if (work_count_ == 0) self->request_stop();
                return self->do_one(true);
            }

            auto run() -> std::size_t {
                auto self = static_cast<Ctx*>(this);
                if (work_count_ == 0) self->request_stop();
                std::size_t count = 0;
                while (run_one()) {
                    if (count < std::numeric_limits<std::size_t>::max()) ++count;
                }
                return count;
            }

        protected:
            std::pmr::polymorphic_allocator<> allocator_;
            inplace_stop_source stop_source_;
            op_queue op_queue_;
            timer_queue timer_queue_{allocator_};
            std::atomic<std::size_t> work_count_{0};
        };
    }

    template<typename ExecutionContext>
    concept execution_context = requires(ExecutionContext& context) {
        { context.get_scheduler() } -> execution::scheduler;
        context.work_started();
        context.work_finished();
    };

    template<execution_context ExecutionContext>
    class work_guard {
    public:
        work_guard() = default;

        explicit work_guard(ExecutionContext& context) noexcept : context_(&context) {
            context.work_started();
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
        ExecutionContext* context_ = nullptr;
    };

    class timed_run_loop : public detail::run_loop_base<timed_run_loop> {
        friend run_loop_base;
    public:
        struct scheduler : scheduler_base {
            using scheduler_base::scheduler_base;
        };

    public:
        using run_loop_base::run_loop_base;

        ~timed_run_loop() = default;

    private:
        auto do_one(bool infinite) -> bool {
            while (not stop_requested()) {
                timer_queue_.take_ready_timers(op_queue_);
                if (const auto op = op_queue_.try_dequeue()) {
                    op->finish();
                    return true;
                }
                if (not infinite) break;
            }
            return false;
        }

        static auto interrupt() noexcept -> void {}
    };
}