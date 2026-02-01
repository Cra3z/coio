// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <limits>
#include <queue>
#include <stop_token>
#include <utility>
#include <vector>
#include "detail/execution.h"
#include "detail/manual_lifetime.h"
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
        class loop_base {
            friend Ctx;
        public:
            class node {
                friend Ctx;
                friend loop_base;
            public:
                node(Ctx& context) noexcept : context_(context) {}

                node(const node&) = delete;

                ~node() = default;

                auto operator= (const node&) -> node& = delete;

                virtual auto finish() noexcept -> void = 0;

            protected:
                COIO_ALWAYS_INLINE auto immediately_post() -> void {
                    COIO_ASSERT(next_ == nullptr);
                    auto& context = context_;
                    context.op_queue_.enqueue(*this);
                    context.interrupt();
                }

            protected:
                Ctx& context_;
                node* next_{};
            };

            template<typename Base>
            class operation_state : public Base {
            private:
                using stop_token_t = stop_token_of_t<execution::env_of_t<decltype(std::declval<Base*>()->rcvr_)>>;

            public:
                using operation_state_concept = execution::operation_state_t;

            public:
                using Base::Base;

                auto start() & noexcept -> void {
                    ++this->context_.work_count_;
                    auto stop_token = coio::get_stop_token(execution::get_env(this->rcvr_));
                    if (stop_token.stop_requested()) {
                        finish();
                        return;
                    }
                    stop_cb_.emplace(
                        std::move(stop_token),
                        std::bind_front(&operation_state::do_cancel, this)
                    );
                    if (not this->do_start()) {
                        finish();
                    }
                }

                auto finish() noexcept -> void override {
                    --this->context_.work_count_;
                    stop_cb_.reset();
                    if (coio::get_stop_token(execution::get_env(this->rcvr_)).stop_requested()) {
                        execution::set_stopped(std::move(this->rcvr_));
                        return;
                    }
                    this->do_finish();
                }

            protected:
                using callback_t = decltype(std::bind_front(&operation_state::do_cancel, std::declval<operation_state*>()));
                std::optional<stop_callback_for_t<stop_token_t, callback_t>> stop_cb_;
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
                template<typename Rcvr>
                struct state_base : node {
                    state_base(Ctx& context, Rcvr rcvr) noexcept : node(context), rcvr_(std::move(rcvr)) {}

                    COIO_ALWAYS_INLINE auto do_start() noexcept -> bool {
                        this->immediately_post();
                        return true;
                    }

                    COIO_ALWAYS_INLINE auto do_finish() noexcept -> void {
                        execution::set_value(std::move(rcvr_));
                    }

                    COIO_ALWAYS_INLINE static auto do_cancel(state_base*) noexcept -> void {}

                    Rcvr rcvr_;
                };

                template<typename Rcvr>
                using state = operation_state<state_base<Rcvr>>;

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

                template<execution::receiver_of<completion_signatures> Rcvr>
                COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                    COIO_ASSERT(ctx_ != nullptr);
                    return state<Rcvr>{*std::exchange(ctx_, {}), std::move(rcvr)};
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

                struct timer_node : node {
                    timer_node(Ctx& context, time_point_type deadline) noexcept: node(context), deadline(deadline) {}

                    time_point_type deadline;
                };

                template<typename Rcvr>
                struct state_base : timer_node {
                    state_base(Rcvr rcvr, Ctx& context, time_point_type deadline) noexcept: timer_node(context, deadline), rcvr_(std::move(rcvr)) {}

                    auto do_start() noexcept -> bool {
                        auto& context = this->context_;
                        if (context.timer_queue_.add(*this)) context.interrupt();
                        return true;
                    }

                    auto do_finish() noexcept -> void {
                        execution::set_value(std::move(this->rcvr_));
                    }

                    auto do_cancel() noexcept -> void {
                        if (this->context_.timer_queue_.remove(*this)) {
                            this->context_.op_queue_.enqueue(*this);
                            this->context_.interrupt();
                        }
                    }

                    Rcvr rcvr_;
                };

                template<typename Rcvr>
                using state = operation_state<state_base<Rcvr>>;

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

                template<execution::receiver_of<completion_signatures> Rcvr>
                COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                    COIO_ASSERT(ctx_ != nullptr);
                    return state<Rcvr>{
                        std::move(rcvr),
                        *std::exchange(ctx_, {}),
                        std::exchange(deadline_, {})
                    };
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
                COIO_ALWAYS_INLINE auto schedule_after(std::chrono::duration<Rep, Period> duration) const noexcept {
                    return this->schedule_at(now() + duration);
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto schedule_at(std::chrono::steady_clock::time_point deadline) const noexcept {
                    return stop_when(sleep_sender{
                        *ctx_,
                        deadline
                    }, ctx_->stop_source_.get_token());
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

            using timer_queue = detail::timer_queue<typename sleep_sender::timer_node, [](const typename sleep_sender::timer_node& op) noexcept {
                return op.deadline;
            }, std::pmr::polymorphic_allocator<>>;

            using op_queue = detail::op_queue<node, &node::next_>;

        private:
            loop_base() = default;

            explicit loop_base(std::pmr::memory_resource& memory_resource) noexcept : allocator_(&memory_resource) {}

        public:
            loop_base(const loop_base&) = delete;

            auto operator= (const loop_base&) -> loop_base& = delete;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_scheduler() noexcept {
                using scheduler_t = typename Ctx::scheduler;
                return scheduler_t{static_cast<Ctx&>(*this)};
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_allocator() const noexcept -> std::pmr::polymorphic_allocator<> {
                return allocator_;
            }

            COIO_ALWAYS_INLINE auto request_stop() -> void {
                auto self = static_cast<Ctx*>(this);
                if (stop_source_.request_stop()) self->interrupt();
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
                return self->do_one(false);
            }

            auto poll() -> std::size_t {
                auto self = static_cast<Ctx*>(this);
                std::size_t count = 0;
                while (poll_one()) {
                    if (count < std::numeric_limits<std::size_t>::max()) ++count;
                }
                return count;
            }

            auto run_one() -> bool {
                auto self = static_cast<Ctx*>(this);
                return self->do_one(true);
            }

            auto run() -> std::size_t {
                auto self = static_cast<Ctx*>(this);
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

    class time_loop : public detail::loop_base<time_loop> {
        friend loop_base;
    public:
        struct scheduler : scheduler_base {
            using scheduler_base::scheduler_base;
        };

    public:
        using loop_base::loop_base;

        ~time_loop() = default;

    private:
        auto do_one(bool infinite) -> bool {
            while (work_count_ > 0) {
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