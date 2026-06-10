#pragma once
#include <csignal> // IWYU pragma: keep
#include <functional>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <coio/utils/when_any.h>
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
            using stop_requested_fn_t = bool(*)(node*) noexcept;

            node(int signum, finish_fn_t finish, stop_requested_fn_t stop_requested) noexcept :
                signum_(signum),
                finish_(finish),
                stop_requested_(stop_requested) {}

            node(const node&) = delete;

            auto operator= (const node&) -> node& = delete;

            auto do_start() noexcept -> void;

            auto do_cancel() noexcept -> void;

            auto stop_requested() noexcept -> bool {
                return stop_requested_(this);
            }

            int signum_;
            const finish_fn_t finish_;
            const stop_requested_fn_t stop_requested_;
            node* prev_ = nullptr;
            node* next_ = nullptr;
        };

        template<typename Rcvr>
        struct op_state : node {
            using stop_token_t = stop_token_of_t<execution::env_of_t<Rcvr>>;

            op_state(int signum, Rcvr rcvr) noexcept :
                node(signum, &finish, &is_stop_requested),
                rcvr(std::move(rcvr)) {}

            op_state(const op_state&) = delete;

            op_state(op_state&&) = delete;

            auto operator= (const op_state&) -> op_state& = delete;

            auto start() & noexcept -> void {
                if (not detail::is_available_signal(signum_)) {
                    finish_(this, -EINVAL);
                    return;
                }

                if constexpr (not unstoppable_token<stop_token_t>) {
                    auto stop_token = coio::get_stop_token(execution::get_env(rcvr));
                    if (stop_token.stop_requested()) {
                        execution::set_stopped(std::move(rcvr));
                        return;
                    }
                    stop_cb.emplace(stop_token, std::bind_front(&op_state::do_cancel, this));
                }

                do_start();
            }

            static auto finish(node* self, int result) noexcept -> void {
                auto this_ = static_cast<op_state*>(self);
                this_->stop_cb.reset();
                if (result < 0) [[unlikely]] {
                    std::error_code ec{-result, std::system_category()};
                    if (ec == std::errc::operation_canceled) {
                        execution::set_stopped(std::move(this_->rcvr));
                        return;
                    }
                    execution::set_error(std::move(this_->rcvr), ec);
                    return;
                }
                execution::set_value(std::move(this_->rcvr), result);
            }

            static auto is_stop_requested(node* self) noexcept -> bool {
                if constexpr (unstoppable_token<stop_token_t>) {
                    return false;
                }
                else {
                    auto this_ = static_cast<op_state*>(self);
                    return coio::get_stop_token(execution::get_env(this_->rcvr)).stop_requested();
                }
            }

            using stop_cb_t = decltype(std::bind_front(&node::do_cancel, std::declval<op_state*>()));
            Rcvr rcvr;
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
        return execution::affine(signal_wait_sender{signal_number});
    }

    template<std::convertible_to<int>... SignalNumbers>
    [[nodiscard]]
    COIO_ALWAYS_INLINE auto signal_wait(SignalNumbers... signal_numbers) noexcept {
        return execution::affine(when_any(signal_wait_sender{static_cast<int>(signal_numbers)}...));
    }

    [[nodiscard]]
    auto strsignal(int signum) noexcept -> std::string_view;
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
