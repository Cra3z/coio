#pragma once
#include <thread>
#include "../core.h"

namespace coio {
    template<timed_scheduler Scheduler>
    class timer {
    public:
        using scheduler = Scheduler;
        using time_point = decltype(std::declval<Scheduler>().now());
        using clock = typename time_point::clock;
        using duration = typename time_point::duration;
        using rep = typename time_point::rep;
        using period = typename time_point::period;

    public:
        explicit timer(Scheduler sched) noexcept : sched_(sched) {}

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_scheduler() const noexcept -> scheduler {
            return sched_;
        }

        template<typename Rep, typename Period>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_wait(std::chrono::duration<Rep, Period> duration) const noexcept {
            return stop_when(sched_.schedule_after(std::chrono::duration_cast<timer::duration>(duration)), stop_source_.get_token());
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_wait_until(time_point deadline) const noexcept {
            return stop_when(sched_.schedule_at(deadline), stop_source_.get_token());
        }

        COIO_ALWAYS_INLINE auto cancel() -> void {
            void(stop_source_.request_stop());
        }

    private:
        Scheduler sched_;
        inplace_stop_source stop_source_;
    };
}