#include <iostream>
#include <doctest/doctest.h>
#include <coio/detail/execution.h>
#include <coio/utils/polymorphic_scheduler.h>

TEST_CASE("scheduler concepts") {
    static_assert(coio::execution::scheduler<coio::polymorphic_scheduler>);
    static_assert(coio::infallible_scheduler<coio::polymorphic_scheduler, coio::execution::env<>>);
}

TEST_CASE("polymorphic_scheduler equality") {
    coio::execution::run_loop loop;
    using run_loop_scheduler = decltype(loop.get_scheduler());
    coio::polymorphic_scheduler sched1(loop.get_scheduler());
    std::optional<run_loop_scheduler> loop_sched = sched1.target<run_loop_scheduler>();
    REQUIRE(loop_sched.has_value());
    CHECK_EQ(loop_sched.value(), loop.get_scheduler());
    coio::polymorphic_scheduler sched2(loop.get_scheduler());
    coio::polymorphic_scheduler sched3 = sched2;
    CHECK_EQ(sched1, loop.get_scheduler());
    CHECK_EQ(sched1, sched2);
    CHECK_EQ(sched1, sched3);
    coio::polymorphic_scheduler sched4(coio::execution::inline_scheduler{});
    CHECK_NE(sched1, sched4);
    CHECK_EQ(sched4, coio::execution::inline_scheduler{});
}

TEST_CASE("polymorphic_scheduler forward-progress-guarantee") {
    coio::execution::run_loop loop;
    coio::polymorphic_scheduler sched1(loop.get_scheduler());
    CHECK_EQ(coio::execution::get_forward_progress_guarantee(sched1), coio::execution::forward_progress_guarantee::parallel);
    coio::polymorphic_scheduler sched2(coio::execution::inline_scheduler{});
    CHECK_EQ(coio::execution::get_forward_progress_guarantee(sched2), coio::execution::forward_progress_guarantee::weakly_parallel);
}

TEST_CASE("polymorphic_scheduler schedules inline scheduler") {
    coio::polymorphic_scheduler sched(coio::execution::inline_scheduler{});
    coio::this_thread::sync_wait(coio::execution::schedule(sched));
}
