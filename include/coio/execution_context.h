// ReSharper disable CppPolymorphicClassWithNonVirtualPublicDestructor
// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <limits>
#include <queue>
#include <semaphore>
#include <thread>
#include <utility>
#include <coio/detail/execution.h>
#include <coio/detail/op_queue.h>
#include <coio/utils/scope_exit.h>
#include <coio/utils/stop_token.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio {
    template<typename T, typename Alloc, typename Sched>
    class task;

    namespace detail {
        enum class operation_phase : unsigned char {
            starting,
            armed,
            cancel_deferred,
            completed
        };

        enum class start_result : unsigned char {
            completed,
            pending
        };

        template<typename Ctx>
        class loop_base {
            friend Ctx;
        public:
            struct node {
                node(Ctx& context) noexcept : context_(context) {}

                node(const node&) = delete;

                ~node() = default;

                auto operator= (const node&) -> node& = delete;

                virtual auto finish() -> void = 0;

                COIO_ALWAYS_INLINE auto immediately_post() -> void {
                    COIO_ASSERT(next_ == nullptr);
                    auto& context = context_;
                    context.op_queue_.enqueue(*this);
                    context.wakeup_consumer();
                }

                auto publish() noexcept -> void {
                    const auto previous = phase_.exchange(operation_phase::completed, std::memory_order_acq_rel);
                    COIO_ASSERT(previous != operation_phase::completed);
                    if (previous == operation_phase::armed) {
                        immediately_post();
                    }
                }

                Ctx& context_;
                node* next_{};
                std::atomic<operation_phase> phase_{operation_phase::starting};
            };

            /// operation_phase state machine:
            ///
            /// [starting]
            ///   |-- request_cancel() --------------------------> [cancel_deferred]
            ///   |-- backend remains pending -------------------> [armed]
            ///   `-- publish() ---------------------------------> [completed] (start posts)
            ///
            /// [cancel_deferred]
            ///   |-- do_start() completes / publish() ----------> [completed] (start posts)
            ///   `-- do_start() remains pending / do_cancel()
            ///         |-- cancellation publishes --------------> [completed] (start posts)
            ///         `-- backend remains pending -------------> [armed]
            ///
            /// [armed]
            ///   |-- request_cancel() / do_cancel() ------------> [armed]
            ///   `-- publish() ---------------------------------> [completed] (publisher posts)
            ///
            /// [completed]
            ///   `-- request_cancel() --------------------------> [completed] (ignored)
            template<typename Base>
            class operation_state : public Base {
            private:
                using stop_token_t = stop_token_of_t<execution::env_of_t<decltype(std::declval<Base*>()->rcvr_)>>;

            public:
                using operation_state_concept = execution::operation_state_tag;

            public:
                using Base::Base;

                auto start() & noexcept -> void {
                    this->context_.work_started();
                    if constexpr (not unstoppable_token<stop_token_t>) {
                        auto stop_token = coio::get_stop_token(execution::get_env(this->rcvr_));
                        stop_cb_.emplace(
                            std::move(stop_token),
                            std::bind_front(&operation_state::request_cancel, this)
                        );
                    }

                    if (this->do_start() == start_result::completed) {
                        this->publish();
                        return this->immediately_post();
                    }

                    auto expected = operation_phase::starting;
                    if (this->phase_.compare_exchange_strong(
                        expected,
                        operation_phase::armed,
                        std::memory_order_release,
                        std::memory_order_acquire
                    )) {
                        return;
                    }

                    if (expected == operation_phase::cancel_deferred) {
                        this->do_cancel();
                        expected = operation_phase::cancel_deferred;
                        if (this->phase_.compare_exchange_strong(
                            expected,
                            operation_phase::armed,
                            std::memory_order_release,
                            std::memory_order_acquire
                        )) {
                            return;
                        }
                    }

                    COIO_ASSERT(expected == operation_phase::completed);
                    return this->immediately_post();
                }

                auto finish() -> void override {
                    this->context_.work_finished();
                    stop_cb_.reset();
                    this->do_finish();
                }

            protected:
                auto request_cancel() noexcept -> void {
                    auto phase = this->phase_.load(std::memory_order_acquire);
                    while (phase == operation_phase::starting) {
                        if (this->phase_.compare_exchange_weak(
                            phase,
                            operation_phase::cancel_deferred,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire
                        )) {
                            return;
                        }
                    }
                    if (phase == operation_phase::armed) {
                        this->do_cancel();
                    }
                }

            private:
                using callback_t = decltype(std::bind_front(&operation_state::request_cancel, std::declval<operation_state*>()));
                std::optional<stop_callback_for_t<stop_token_t, callback_t>> stop_cb_;
            };

            struct env {
                template<typename Tag>
                auto query(execution::get_completion_scheduler_t<Tag>) const noexcept {
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
                struct state : node {
                    using operation_state_concept = execution::operation_state_tag;

                    state(Ctx& context, Rcvr rcvr) noexcept : node(context), rcvr_(std::move(rcvr)) {}

                    COIO_ALWAYS_INLINE auto start() noexcept -> void {
                        this->context_.work_started();
                        this->immediately_post();
                    }

                    auto finish() noexcept -> void override {
                        this->context_.work_finished();
                        if constexpr (not unstoppable_token<stop_token_of_t<execution::env_of_t<Rcvr>>>) {
                            auto stop_token = get_stop_token(execution::get_env(rcvr_));
                            if (stop_token.stop_requested()) {
                                execution::set_stopped(std::move(rcvr_));
                                return;
                            }
                        }
                        execution::set_value(std::move(rcvr_));
                    }

                    Rcvr rcvr_;
                };

            public:
                using sender_concept = execution::sender_tag;

            public:
                explicit schedule_sender(Ctx& context) noexcept : ctx_(&context) {}

                COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                    return env{*ctx_};
                }

                template<similar_to<schedule_sender>, typename Env>
                static consteval auto get_completion_signatures() noexcept {
                    if constexpr (unstoppable_token<stop_token_of_t<Env>>) {
                        return execution::completion_signatures<execution::set_value_t()>{};
                    }
                    else {
                        return execution::completion_signatures<execution::set_value_t(), execution::set_stopped_t()>{};
                    }
                }

                template<execution::receiver Rcvr>
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
                using sender_concept = execution::sender_tag;
                using completion_signatures = execution::completion_signatures<
                    execution::set_value_t(),
                    execution::set_stopped_t()
                >;
                using clock_type = std::chrono::steady_clock;
                using duration_type = clock_type::duration;
                using time_point_type = clock_type::time_point;

                struct timer_node : node {
                    timer_node(Ctx& context, time_point_type deadline) noexcept: node(context), deadline(deadline) {}
                    time_point_type deadline;
                    detail::timer_heap_links<timer_node> heap_links;
                };

                template<typename Rcvr>
                struct state_base : timer_node {
                    state_base(Rcvr rcvr, Ctx& context, time_point_type deadline) noexcept: timer_node(context, deadline), rcvr_(std::move(rcvr)) {}

                    auto do_start() noexcept -> start_result {
                        auto& context = this->context_;
                        if (context.timer_queue_.add(*this)) context.wakeup_consumer();
                        return start_result::pending;
                    }

                    auto do_finish() noexcept -> void {
                        if (canceled_) execution::set_stopped(std::move(rcvr_));
                        else execution::set_value(std::move(rcvr_));
                    }

                    auto do_cancel() noexcept -> void {
                        if (this->context_.timer_queue_.remove(*this)) {
                            canceled_ = true;
                            this->publish();
                        }
                    }

                    Rcvr rcvr_;
                    bool canceled_ = false;
                };

                template<typename Rcvr>
                using state = operation_state<state_base<Rcvr>>;

            public:
                sleep_sender(Ctx& context, time_point_type deadline) noexcept : ctx_(&context), deadline_(deadline) {}

                sleep_sender(const sleep_sender&) = delete;

                sleep_sender(sleep_sender&& other) noexcept : ctx_(std::exchange(other.ctx_, {})), deadline_(std::exchange(other.deadline_, {})) {}

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

                template<similar_to<sleep_sender>, typename...>
                static consteval auto get_completion_signatures() noexcept -> completion_signatures {
                    return {};
                }

                template<execution::receiver Rcvr>
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
                using scheduler_concept = execution::scheduler_tag;

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
                    return this->schedule_at(now() + std::chrono::ceil<std::chrono::steady_clock::duration>(duration));
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

                [[nodiscard]]
                COIO_ALWAYS_INLINE static constexpr auto query(execution::get_forward_progress_guarantee_t) noexcept {
                    return execution::forward_progress_guarantee::parallel;
                }

                [[nodiscard]]
                COIO_ALWAYS_INLINE auto query(get_allocator_t) const noexcept {
                    COIO_ASSERT(ctx_ != nullptr);
                    return ctx_->get_allocator();
                }

                friend auto operator== (const scheduler_base& lhs, const scheduler_base& rhs) -> bool = default;

            protected:
                Ctx* ctx_;
            };

            using timer_queue = detail::timer_queue<
                typename sleep_sender::timer_node,
                &sleep_sender::timer_node::deadline,
                &sleep_sender::timer_node::heap_links
            >;

            using op_queue = detail::op_queue<node, &node::next_>;

        private:
            loop_base() = default;

            explicit loop_base(std::pmr::memory_resource& memory_resource) noexcept : allocator_(&memory_resource) {}

            ~loop_base() {
                if (work_count_.load(std::memory_order_acquire) != 0) [[unlikely]] {
                    std::terminate();
                }
            }

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
                if (stop_source_.request_stop()) shutdown();
            }

            COIO_ALWAYS_INLINE auto work_started() noexcept -> void {
                ++work_count_;
            }

            COIO_ALWAYS_INLINE auto work_finished() noexcept -> void {
                if (--work_count_ == 0) shutdown();
            }

            auto poll_one() -> bool {
                auto self = static_cast<Ctx*>(this);
                consumer_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);
                scope_exit _{[this]() noexcept { consumer_id_.store({}, std::memory_order_relaxed); }};
                return self->do_one(false);
            }

            auto poll() -> std::size_t {
                auto self = static_cast<Ctx*>(this);
                consumer_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);
                scope_exit _{[this]() noexcept { consumer_id_.store({}, std::memory_order_relaxed); }};
                std::size_t count = 0;
                while (self->do_one(false)) {
                    if (count < std::numeric_limits<std::size_t>::max()) ++count;
                }
                return count;
            }

            auto run_one() -> bool {
                auto self = static_cast<Ctx*>(this);
                consumer_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);
                scope_exit _{[this]() noexcept { consumer_id_.store({}, std::memory_order_relaxed); }};
                return self->do_one(true);
            }

            auto run() -> std::size_t {
                auto self = static_cast<Ctx*>(this);
                consumer_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);
                scope_exit _{[this]() noexcept { consumer_id_.store({}, std::memory_order_relaxed); }};
                std::size_t count = 0;
                while (self->do_one(true)) {
                    if (count < std::numeric_limits<std::size_t>::max()) ++count;
                }
                return count;
            }

        protected:
            COIO_ALWAYS_INLINE static auto publish_pending(node* op) noexcept -> void {
                while (op != nullptr) {
                    auto next = std::exchange(op->next_, nullptr);
                    op->publish();
                    op = next;
                }
            }

            COIO_ALWAYS_INLINE auto consume() -> bool {
                node* op = op_queue_.dequeue();
                if (op) op->finish();
                return op;
            }

            COIO_ALWAYS_INLINE auto wakeup_consumer() -> void {
                if (consumer_id_.load(std::memory_order_relaxed) == std::this_thread::get_id()) return;
                static_cast<Ctx*>(this)->interrupt();
            }

            COIO_ALWAYS_INLINE auto shutdown() -> void {
                wakeup_consumer();
            }

        protected:
            std::pmr::polymorphic_allocator<> allocator_;
            inplace_stop_source stop_source_;
            op_queue op_queue_;
            timer_queue timer_queue_;
            std::atomic<std::size_t> work_count_{0};
            std::atomic<std::thread::id> consumer_id_{};
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

        template<typename T = void, typename Alloc = void>
        using task = coio::task<T, Alloc, scheduler>;

    public:
        explicit time_loop(std::pmr::memory_resource& resource = *std::pmr::get_default_resource()) noexcept : loop_base(resource) {}

        ~time_loop() = default;

    private:
        auto do_one(bool infinite) -> bool {
            if (work_count_ == 0) return false;

            while (work_count_ > 0) {
                if (consume()) {
                    return true;
                }

                if (infinite) {
                    if (const auto earliest = timer_queue_.earliest()) {
                        static_cast<void>(sema_.try_acquire_until(*earliest));
                    }
                    else {
                        sema_.acquire();
                    }
                }

                detail::intrusive_list<node> ready_time_ops{&node::next_};
                timer_queue_.take_ready_timers(ready_time_ops);

                publish_pending(ready_time_ops.release());

                if (not infinite) {
                    return consume();
                }
            }
            return false;
        }

        COIO_ALWAYS_INLINE auto interrupt() noexcept -> void {
            sema_.release();
        }

    private:
        std::counting_semaphore<> sema_{0};
    };
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
