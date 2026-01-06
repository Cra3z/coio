#pragma once
#include <thread>
#include "../schedulers.h"

namespace coio {
    template<timed_scheduler Scheduler>
    class timer {
    public:
        using scheduler = Scheduler;
        using sender = decltype(std::declval<Scheduler>().schedule_at(std::declval<Scheduler>().now()));
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
        COIO_ALWAYS_INLINE auto wait(std::chrono::duration<Rep, Period> duration) const -> void {
            std::this_thread::sleep_for(duration);
        }

        COIO_ALWAYS_INLINE auto wait_until(time_point deadline) const -> void {
            std::this_thread::sleep_until(deadline);
        }

        template<typename Rep, typename Period>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_wait(std::chrono::duration<Rep, Period> duration) const noexcept -> sender {
            return sched_.schedule_after(std::chrono::duration_cast<timer::duration>(duration));
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_wait_until(time_point deadline) const noexcept -> sender {
            return sched_.schedule_at(deadline);
        }

    private:
        Scheduler sched_;
    };
}