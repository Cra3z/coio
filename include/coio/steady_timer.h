#pragma once
#include <thread>
#include "execution_context.h"

namespace coio {
    class steady_timer {
    public:
        using sleep_sender = execution_context::sleep_sender;
        using clock_type = sleep_sender::clock_type;
        using duration_type = sleep_sender::duration_type;
        using time_point_type = sleep_sender::time_point_type;

    public:
        explicit steady_timer(execution_context& context) noexcept : context_(&context) {}

        auto wait(duration_type duration) const -> void {
            std::this_thread::sleep_for(duration);
        }

        auto wait_until(time_point_type deadline) const -> void {
            std::this_thread::sleep_until(deadline);
        }

        [[nodiscard]]
        auto async_wait(duration_type duration) const noexcept -> sleep_sender {
            return async_wait_until(clock_type::now() + duration);
        }

        [[nodiscard]]
        auto async_wait_until(time_point_type deadline) const noexcept -> sleep_sender {
            return {*context_, deadline};
        }

        [[nodiscard]]
        auto context() const noexcept -> execution_context& {
            return *context_;
        }

    private:
        execution_context* context_; // not null
    };
}