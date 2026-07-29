#pragma once
#include <atomic>
#include <csignal> // IWYU pragma: keep
#include <functional>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <coio/utils/when_any.h>
#include <coio/execution_context.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio {
    namespace detail {
        class signal_state;

        auto is_available_signal(int signal_number) noexcept -> bool;
    }

    struct signal_wait_many_sender;

    struct signal_wait_sender {
        friend detail::signal_state;
        friend signal_wait_many_sender;
    private:
        struct node {
            using operation_state_concept = execution::operation_state_tag;
            using finish_fn_t = void(*)(node*, int) noexcept;

            node(int signum, finish_fn_t finish) noexcept :
                signum_(signum),
                finish_(finish) {}

            node(const node&) = delete;

            auto operator= (const node&) -> node& = delete;

            auto do_start() noexcept -> int;

            auto do_cancel() noexcept -> void;

            int signum_;
            const finish_fn_t finish_;
            std::atomic<detail::operation_phase> phase_{detail::operation_phase::starting};
            node* prev_ = nullptr;
            node* next_ = nullptr;
        };

        template<typename Rcvr>
        struct op_state : node {
            using stop_token_t = stop_token_of_t<execution::env_of_t<Rcvr>>;

            op_state(int signum, Rcvr rcvr) noexcept :
                node(signum, &finish),
                rcvr(std::move(rcvr)) {}

            op_state(const op_state&) = delete;

            op_state(op_state&&) = delete;

            auto operator= (const op_state&) -> op_state& = delete;

            auto start() & noexcept -> void {
                if constexpr (not unstoppable_token<stop_token_t>) {
                    auto stop_token = coio::get_stop_token(execution::get_env(rcvr));
                    stop_cb.emplace(stop_token, std::bind_front(&op_state::request_cancel, this));
                }

                if (not detail::is_available_signal(signum_)) {
                    finish_(this, -EINVAL);
                    deliver(this);
                    return;
                }

                if (const int result = do_start(); result != 0) {
                    finish_(this, result);
                    deliver(this);
                    return;
                }

                auto expected = detail::operation_phase::starting;
                if (phase_.compare_exchange_strong(
                    expected,
                    detail::operation_phase::armed,
                    std::memory_order_release,
                    std::memory_order_acquire
                )) {
                    return;
                }

                if (expected == detail::operation_phase::cancel_deferred) {
                    do_cancel();
                    expected = detail::operation_phase::cancel_deferred;
                    if (phase_.compare_exchange_strong(
                        expected,
                        detail::operation_phase::armed,
                        std::memory_order_release,
                        std::memory_order_acquire
                    )) {
                        return;
                    }
                }

                COIO_ASSERT(expected == detail::operation_phase::completed);
                deliver(this);
            }

            static auto finish(node* self, int result) noexcept -> void {
                auto this_ = static_cast<op_state*>(self);
                this_->result_ = result;
                const auto previous = this_->phase_.exchange(
                    detail::operation_phase::completed,
                    std::memory_order_acq_rel
                );
                COIO_ASSERT(previous != detail::operation_phase::completed);
                if (previous == detail::operation_phase::armed) deliver(this_);
            }

            static auto deliver(op_state* this_) noexcept -> void {
                this_->stop_cb.reset();
                if (this_->result_ < 0) [[unlikely]] {
                    std::error_code ec{-this_->result_, std::system_category()};
                    if (ec == std::errc::operation_canceled) {
                        execution::set_stopped(std::move(this_->rcvr));
                        return;
                    }
                    execution::set_error(std::move(this_->rcvr), ec);
                    return;
                }
                execution::set_value(std::move(this_->rcvr), this_->result_);
            }

            auto request_cancel() noexcept -> void {
                auto phase = phase_.load(std::memory_order_acquire);
                while (phase == detail::operation_phase::starting) {
                    if (phase_.compare_exchange_weak(
                        phase,
                        detail::operation_phase::cancel_deferred,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire
                    )) {
                        return;
                    }
                }
                if (phase == detail::operation_phase::armed) {
                    do_cancel();
                }
            }

            using stop_cb_t = decltype(std::bind_front(&op_state::request_cancel, std::declval<op_state*>()));
            Rcvr rcvr;
            int result_ = 0;
            std::optional<stop_callback_for_t<stop_token_t, stop_cb_t>> stop_cb;
        };

    public:
        using sender_concept = execution::sender_tag;
        using completion_signatures = execution::completion_signatures<
            execution::set_value_t(int),
            execution::set_error_t(std::error_code),
            execution::set_stopped_t()
        >;

    public:
        explicit signal_wait_sender(int signum) noexcept : signum(signum) {}

        signal_wait_sender(const signal_wait_sender&) = delete;

        signal_wait_sender(signal_wait_sender&& other) noexcept : signum(std::exchange(other.signum, 0)) {}

        auto operator= (signal_wait_sender other) noexcept -> signal_wait_sender& {
            std::swap(signum, other.signum);
            return *this;
        }

        template<execution::receiver Rcvr>
        COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
            COIO_ASSERT(signum != 0);
            return op_state<Rcvr>{std::exchange(signum, 0), std::move(rcvr)};
        }

        template<similar_to<signal_wait_sender>, typename...>
        static consteval auto get_completion_signatures() noexcept -> completion_signatures {
            return completion_signatures{};
        }

    private:
        int signum;
    };

    [[nodiscard]]
    COIO_ALWAYS_INLINE auto signal_wait(int signal_number) noexcept {
        return append_fallback_env(
            execution::affine(signal_wait_sender{signal_number}),
            execution::prop{execution::get_start_scheduler, execution::inline_scheduler{}}
        );
    }

    template<std::convertible_to<int>... SignalNumbers>
    [[nodiscard]]
    COIO_ALWAYS_INLINE auto signal_wait(SignalNumbers... signal_numbers) noexcept {
        static_assert((... and std::is_nothrow_convertible_v<SignalNumbers, int>));
        return append_fallback_env(
            execution::affine(when_any(signal_wait_sender{static_cast<int>(signal_numbers)}...)),
            execution::prop{execution::get_start_scheduler, execution::inline_scheduler{}}
        );
    }

    [[nodiscard]]
    auto strsignal(int signum) noexcept -> std::string_view;
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
