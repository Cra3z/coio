#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <coio/core.h>
#include <coio/utils/fifo.h>

TEST_CASE("fifo preserves order with try operations") {
    coio::fifo<std::string> queue;

    CHECK(queue.empty());
    CHECK(queue.try_push("one"));
    CHECK(queue.try_push("two"));
    CHECK(queue.size() == 2);

    auto first = queue.try_pop();
    REQUIRE(first.has_value());
    CHECK(*first == "one");

    auto second = queue.try_pop();
    REQUIRE(second.has_value());
    CHECK(*second == "two");

    CHECK_FALSE(queue.try_pop().has_value());
}

TEST_CASE("fifo hands off values between async producers and consumers") {
    coio::fifo<std::string> queue;
    std::vector<std::string> popped;
    popped.reserve(3);

    coio::this_thread::sync_wait(coio::when_all(
        [&]() -> coio::task<> {
            popped.push_back(co_await queue.async_pop());
            popped.push_back(co_await queue.async_pop());
            popped.push_back(co_await queue.async_pop());
        }(),
        [&]() -> coio::task<> {
            co_await queue.async_push("one");
            co_await queue.async_push("two");
            co_await queue.async_push("three");
        }()
    ));

    CHECK_EQ(popped, std::vector<std::string>{"one", "two", "three"});
}
