#include <memory>
#include <stdexcept>
#include <string>
#include <doctest/doctest.h>
#include <coio/core.h>

namespace {
    template<typename T>
    struct tagged_allocator {
        using value_type = T;

        tagged_allocator() noexcept = default;
        explicit tagged_allocator(int tag) noexcept : tag(tag) {}

        template<typename U>
        tagged_allocator(const tagged_allocator<U>& other) noexcept : tag(other.tag) {}

        auto allocate(std::size_t count) -> T* {
            return static_cast<T*>(::operator new(count * sizeof(T)));
        }

        auto deallocate(T* ptr, std::size_t) noexcept -> void {
            ::operator delete(ptr);
        }

        template<typename U>
        struct rebind {
            using other = tagged_allocator<U>;
        };

        template<typename U>
        friend auto operator==(const tagged_allocator& lhs, const tagged_allocator<U>& rhs) noexcept -> bool {
            return lhs.tag == rhs.tag;
        }

        int tag = 0;
    };

    using task_allocator = tagged_allocator<std::byte>;

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

    auto check_scheduler(auto expected) -> coio::task<> {
        auto this_scheduler = co_await coio::execution::read_env(coio::execution::get_scheduler);
        CHECK_EQ(this_scheduler, expected);
    }

    auto read_allocator() -> coio::task<task_allocator, task_allocator> {
        auto allocator = co_await coio::read_allocator();
        co_return allocator;
    }

    auto forward_allocator() -> coio::task<task_allocator, task_allocator> {
        co_return co_await read_allocator();
    }
}

static_assert(std::same_as<coio::task<>::allocator_type, std::allocator<std::byte>>);

TEST_CASE("task returns values and composes with co_await") {
    auto result = coio::this_thread::sync_wait(chain_task(5));
    REQUIRE(result.has_value());

    auto [value] = result.value();
    CHECK_EQ(value, 12);
}

TEST_CASE("task<void> completes normally") {
    std::string marker;
    coio::this_thread::sync_wait(write_marker(marker));
    CHECK_EQ(marker, "task<void>");
}

TEST_CASE("task propagates exceptions") {
    CHECK_THROWS_AS(coio::this_thread::sync_wait(fail_task()), std::runtime_error);
}

TEST_CASE("task promise reads allocator from receiver environment") {
    constexpr int expected_tag = 17;

    auto sender = coio::execution::write_env(
        read_allocator(),
        coio::execution::prop{
            coio::get_allocator,
            task_allocator{expected_tag}});
    auto result = coio::this_thread::sync_wait(std::move(sender));

    REQUIRE(result.has_value());
    auto [allocator] = result.value();
    CHECK_EQ(allocator.tag, expected_tag);
}

TEST_CASE("task forwards receiver allocator through nested await") {
    constexpr int expected_tag = 23;

    auto sender = coio::execution::write_env(
        forward_allocator(),
        coio::execution::prop{
            coio::get_allocator,
            task_allocator{expected_tag}});
    auto result = coio::this_thread::sync_wait(std::move(sender));

    REQUIRE(result.has_value());
    auto [allocator] = result.value();
    CHECK_EQ(allocator.tag, expected_tag);
}

TEST_CASE("task scheduler") {
    SUBCASE("run_loop") {
        coio::execution::run_loop loop;
        auto loop_scheduler = loop.get_scheduler();
        using loop_scheduler_t = decltype(loop_scheduler);
        std::jthread thrd{[&] {
            loop.run();
        }};
        coio::this_thread::sync_wait(coio::starts_on(loop_scheduler, check_scheduler(loop_scheduler)));
        coio::this_thread::sync_wait(coio::starts_on(loop_scheduler, [&]() -> coio::task<void, std::allocator<std::byte>, loop_scheduler_t> {
            auto this_scheduler = co_await coio::execution::read_env(coio::execution::get_scheduler);
            static_assert(std::same_as<loop_scheduler_t, decltype(this_scheduler)>);
            CHECK_EQ(loop_scheduler, this_scheduler);
        }()));
        loop.finish();
    }
    SUBCASE("inline_scheduler") {
        coio::async_scope scope;
        scope.spawn(check_scheduler(coio::execution::inline_scheduler{}));
        coio::this_thread::sync_wait(scope.join());
    }
}
