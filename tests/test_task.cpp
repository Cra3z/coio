#include <stdexcept>
#include <string>
#include <doctest/doctest.h>
#include <coio/core.h>

namespace {
auto add_one(int value) -> coio::task<int> {
    co_return value + 1;
}

auto chain_task(int value) -> coio::task<int> {
    co_return (co_await add_one(value)) * 2;
}

auto write_marker(std::string& marker) -> coio::task<> {
    marker = "task<void>";
    co_return;
}

auto fail_task() -> coio::task<int> {
    throw std::runtime_error{"task failure"};
}
}

TEST_CASE("task returns values and composes with co_await") {
    auto result = coio::this_thread::sync_wait(chain_task(5));
    REQUIRE(result.has_value());

    auto [value] = result.value();
    CHECK(value == 12);
}

TEST_CASE("task<void> completes normally") {
    std::string marker;
    coio::this_thread::sync_wait(write_marker(marker));
    CHECK(marker == "task<void>");
}

TEST_CASE("task propagates exceptions") {
    CHECK_THROWS_AS(coio::this_thread::sync_wait(fail_task()), std::runtime_error);
}
