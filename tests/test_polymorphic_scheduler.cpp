#include <iostream>
#include <doctest/doctest.h>
#include <coio/detail/execution.h>
#include <coio/utils/polymorphic_scheduler.h>

TEST_CASE("scheduler concepts") {
    static_assert(coio::execution::scheduler<coio::polymorphic_scheduler>);
    static_assert(coio::detail::infallible_scheduler<coio::polymorphic_scheduler, coio::execution::env<>>);
}

TEST_CASE("polymorphic_scheduler equality") {
    coio::execution::run_loop loop;
    coio::polymorphic_scheduler sched1(loop.get_scheduler());
    coio::polymorphic_scheduler sched2(loop.get_scheduler());
    coio::polymorphic_scheduler sched3 = sched2;
    CHECK(sched1 == loop.get_scheduler());
    CHECK(sched1 == sched2);
    CHECK(sched1 == sched3);
    coio::polymorphic_scheduler sched4(coio::execution::inline_scheduler{});
    CHECK(sched1 != sched4);
    CHECK(sched4 == coio::execution::inline_scheduler{});
}

TEST_CASE("polymorphic_scheduler forward-progress-guarantee") {
    coio::execution::run_loop loop;
    coio::polymorphic_scheduler sched1(loop.get_scheduler());
    CHECK(coio::execution::get_forward_progress_guarantee(sched1) == coio::execution::forward_progress_guarantee::parallel);
    coio::polymorphic_scheduler sched2(coio::execution::inline_scheduler{});
    CHECK(coio::execution::get_forward_progress_guarantee(sched2) == coio::execution::forward_progress_guarantee::weakly_parallel);
}
