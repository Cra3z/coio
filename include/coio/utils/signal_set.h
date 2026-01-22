#pragma once
#include <csignal> // IWYU pragma: keep
#include <set>
#include "../detail/intrusive_stack.h"
#include "../task.h"

namespace coio {
    namespace detail {
        class signal_state;
    }

    class signal_set {
        friend detail::signal_state;
    private:
        class awaiter {
            friend signal_set;
            friend detail::signal_state;
        public:
            explicit awaiter(signal_set& owner) noexcept : owner_{owner} {};

            awaiter(const awaiter&) = delete;

            auto operator= (const awaiter&) -> awaiter& = delete;

            static auto await_ready() noexcept -> bool {
                return false;
            }

            template<typename Promise>
            auto await_suspend(std::coroutine_handle<Promise> this_coro) -> void {
                coro_ = this_coro;
                if constexpr (stoppable_promise<Promise>) {
                    unhandled_stopped_ = &detail::stop_coroutine<Promise>;
                }
                owner_.awaiters_.push(*this);
            }

            auto await_resume() const noexcept -> int {
                return signal_number_;
            }

        private:
            signal_set& owner_;
            int signal_number_ = 0;
            std::coroutine_handle<> coro_{};
            detail::unhandled_stopped_fn unhandled_stopped_ = &detail::default_unhandled_stopped_;
            awaiter* next_ = nullptr;
        };

    public:
        struct awaitable {
            COIO_ALWAYS_INLINE auto operator co_await() && noexcept {
                COIO_ASSERT(owner != nullptr);
                return awaiter{*std::exchange(owner, {})};
            }

            signal_set* owner = nullptr;
        };

    public:
        signal_set() = default;

        explicit signal_set(std::initializer_list<int> signal_numbers) {
            for (int signum : signal_numbers) add(signum);
        }

        signal_set(const signal_set&) = delete;

        ~signal_set();

        auto operator= (const signal_set&) -> signal_set& = delete;

        [[nodiscard]]
        auto async_wait() noexcept -> awaitable {
            return {this};
        }

        auto add(int signal_number) -> void;

        auto remove(int signal_number) -> void;

        auto cancel() -> void;

        auto clear() -> void;

    private:
        std::set<int> signal_numbers_;
        detail::intrusive_stack<awaiter> awaiters_{&awaiter::next_};
    };
}