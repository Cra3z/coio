#pragma once
#include <functional>
#include <variant>
#include "../utils/stop_token.h"
#include "unhandled_stopped.h"

namespace coio::detail {
    template<typename Base, typename StopToken>
    class stoppable_op : public Base {
    private:
        using callback_t = decltype(std::bind_front(&Base::cancel, std::declval<stoppable_op*>()));

    public:
        template<typename... Args>
        stoppable_op(StopToken token, Args&&... args) noexcept :
            Base(std::forward<Args>(args)...), stop_ctl_(std::move(token)) {}

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> this_coro) noexcept -> std::coroutine_handle<> {
            StopToken stop_token = std::get<StopToken>(std::move(stop_ctl_));
            if constexpr (stoppable_promise<Promise>) {
                if (stop_token.stop_requested()) return (stop_stoppable_coroutine_<Promise>)(this_coro);
            }
            const auto suspended = this->await_suspend_impl(this_coro);
            stop_ctl_.template emplace<stop_callback_for_t<StopToken, callback_t>>(
                std::move(stop_token),
                std::bind_front(&Base::cancel, this)
            );
            return suspended ? std::noop_coroutine() : std::coroutine_handle<>{this_coro};
        }

        [[nodiscard]]
        decltype(auto) await_resume() {
            stop_ctl_.template emplace<std::monostate>();
            return this->await_resume_impl();
        }

    private:
        COIO_NO_UNIQUE_ADDRESS std::variant<std::monostate, StopToken, stop_callback_for_t<StopToken, callback_t>> stop_ctl_;
    };
}