#pragma once
#include <array>
#include <coroutine>
#include <concepts>
#include <type_traits>
#include <utility>
#include "core.h"

namespace coio {

    template<typename T>
    concept scheduler = requires (T t) {
        { t.schedule() } -> awaitable;
    };


    struct noop_scheduler {
        using schedule_operation = std::suspend_never;

        [[nodiscard]]
        static constexpr auto schedule() noexcept -> schedule_operation { return {}; }
    };


    template<std::size_t N>
    class round_robin_scheduler {
    public:

        class schedule_operation {
        private:

            explicit schedule_operation(round_robin_scheduler& scheduler) noexcept : scheduler(scheduler) {}

        public:
            static auto await_ready() noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> this_coro) const noexcept -> std::coroutine_handle<> {
                return scheduler.select_and_exchange(this_coro);
            }

            static auto await_resume() noexcept -> void {}
        private:
            round_robin_scheduler& scheduler;
        };

    public:
        round_robin_scheduler() noexcept {
            coros_.fill(std::noop_coroutine());
        }

        round_robin_scheduler(const round_robin_scheduler&) = delete;

        auto operator= (const round_robin_scheduler&) -> round_robin_scheduler& = delete;

        [[nodiscard]]
        auto schedule() noexcept -> schedule_operation {
            return schedule_operation{*this};
        }

    private:
        auto select_and_exchange(std::coroutine_handle<> new_coro) noexcept -> std::coroutine_handle<> {
            return std::exchange(coros_[std::exchange(index_, (index_ + 1) % N)], new_coro);
        }

    private:
        std::size_t index_ = 0;
        std::array<std::coroutine_handle<>, N> coros_;
    };

}