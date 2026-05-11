#include <string_view>
#include <vector>
#include <doctest/doctest.h>
#include <coio/core.h>
#include <coio/sync_primitives.h>

TEST_CASE("async_latch waits until the counter reaches zero") {
    using namespace std::string_view_literals;

    coio::async_latch<> latch{3};
    std::vector<std::string_view> order;
    coio::async_scope scope;

    scope.spawn(latch.wait() | coio::then([&]() noexcept {
        order.emplace_back("#2");
    }));

    scope.spawn(latch.arrive_and_wait(2) | coio::then([&]() noexcept {
        order.emplace_back("#1");
    }));

    CHECK(latch.count() == 1);
    CHECK_FALSE(latch.try_wait());
    latch.count_down();

    coio::this_thread::sync_wait(scope.join());

    CHECK(latch.try_wait());
    CHECK_EQ(order, std::vector{"#1"sv, "#2"sv});
}
