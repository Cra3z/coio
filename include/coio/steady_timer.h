#pragma once
#include <thread>
#include "execution_context.h"

namespace coio {
    class steady_timer {
    public:
        using sleep_operation = execution_context::sleep_operation;
        using clock_type = sleep_operation::clock_type;
        using duration_type = sleep_operation::duration_type;
        using time_point_type = sleep_operation::time_point_type;

    public:
        explicit steady_timer(execution_context& context) noexcept : context_(&context) {}

        steady_timer(execution_context& context, time_point_type timeout) noexcept :
            context_(&context), deadline_(timeout) {}

        steady_timer(execution_context& context, duration_type duration) noexcept :
            steady_timer(context, clock_type::now() + duration) {}

        steady_timer(steady_timer&& other) noexcept :
            context_(other.context_), deadline_(std::exchange(other.deadline_, {})) {}

        auto operator= (steady_timer other) noexcept -> steady_timer& {
            std::swap(other.deadline_, deadline_);
            std::swap(other.context_, context_);
            return *this;
        }

        auto wait() const -> void {
            std::this_thread::sleep_until(deadline_);
        }

        [[nodiscard]]
        auto async_wait() const noexcept -> sleep_operation {
            return {*context_, deadline_};
        }

        [[nodiscard]]
        auto context() const noexcept -> execution_context& {
            return *context_;
        }

    private:
        execution_context* context_; // not null
        time_point_type deadline_;
    };
}