#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <doctest/doctest.h>
#include <coio/asyncio/io.h>
#include <coio/utils/flat_buffer.h>

namespace {
    auto make_payload(std::size_t size) -> std::vector<std::byte> {
        std::vector<std::byte> payload(size);
        for (std::size_t i = 0; i < size; ++i) {
            payload[i] = static_cast<std::byte>((i * 131 + 17) & 0xff);
        }
        return payload;
    }

    auto fill(coio::flat_buffer& buffer, std::span<const std::byte> bytes) -> void {
        std::ranges::copy(bytes, buffer.prepare(bytes.size()).begin());
        buffer.commit(bytes.size());
    }

    // Refuses to allocate while armed, so the not_enough_memory path can be reached deliberately.
    template<typename T>
    struct failing_allocator {
        using value_type = T;

        failing_allocator() noexcept = default;

        explicit failing_allocator(const bool* armed) noexcept : armed(armed) {}

        template<typename U>
        failing_allocator(const failing_allocator<U>& other) noexcept : armed(other.armed) {}

        auto allocate(std::size_t n) -> T* {
            if (armed and *armed) throw std::bad_alloc{};
            return std::allocator<T>{}.allocate(n);
        }

        auto deallocate(T* p, std::size_t n) noexcept -> void {
            std::allocator<T>{}.deallocate(p, n);
        }

        friend auto operator== (const failing_allocator&, const failing_allocator&) noexcept -> bool = default;

        const bool* armed = nullptr;
    };

    using fallible_buffer = coio::basic_flat_buffer<failing_allocator<std::byte>>;

    static_assert(coio::dynamic_buffer<coio::flat_buffer>);
    static_assert(coio::dynamic_buffer<fallible_buffer>);
}

// `prepare` owes the strong guarantee (as Beast documents for its own flat_buffer): a caller that
// handles the failure and falls back to a smaller request must still find its data.
TEST_CASE("flat_buffer is unchanged when prepare exceeds max_size") {
    coio::flat_buffer buffer{64};
    const auto payload = make_payload(40);
    fill(buffer, payload);
    buffer.consume(10); // so a prepare would have to compact before it could grow

    const auto expected = std::span{payload}.subspan(10);
    REQUIRE(std::ranges::equal(buffer.data(), expected));

    std::error_code ec;
    const auto prepared = buffer.prepare(40, ec); // 30 readable + 40 > max_size() == 64
    CHECK_EQ(ec, std::errc::no_buffer_space);
    CHECK(prepared.empty());
    CHECK_EQ(buffer.size(), 30);
    CHECK(std::ranges::equal(buffer.data(), expected));

    CHECK_THROWS_AS(static_cast<void>(buffer.prepare(40)), std::length_error);
    CHECK(std::ranges::equal(buffer.data(), expected));

    CHECK_EQ(buffer.prepare(30).size(), 30);
}

// Without this the fast paths in `prepare`, which only consult the allocation, would happily hand
// out writable bytes past `max_size()`.
TEST_CASE("flat_buffer never grows its capacity past max_size") {
    coio::flat_buffer buffer{64};
    CHECK_LE(buffer.capacity(), buffer.max_size());

    static_cast<void>(buffer.prepare(40));
    buffer.commit(40);
    CHECK_LE(buffer.capacity(), buffer.max_size());

    buffer.consume(10);
    CHECK_THROWS_AS(static_cast<void>(buffer.prepare(40)), std::length_error); // 30 + 40 > 64
    CHECK_LE(buffer.size(), buffer.max_size());
}

TEST_CASE("flat_buffer reports not_enough_memory and stays intact when the allocator refuses") {
    bool armed = false;
    fallible_buffer buffer{1024, failing_allocator<std::byte>{&armed}};

    const auto payload = make_payload(40);
    std::ranges::copy(payload, buffer.prepare(payload.size()).begin());
    buffer.commit(payload.size());
    const std::size_t capacity_before = buffer.capacity();

    armed = true;
    std::error_code ec;
    const auto prepared = buffer.prepare(900, ec); // needs to grow past the current capacity
    CHECK_EQ(ec, std::errc::not_enough_memory);
    CHECK(prepared.empty());
    CHECK_EQ(buffer.size(), payload.size());
    CHECK(std::ranges::equal(buffer.data(), payload));
    CHECK_EQ(buffer.capacity(), capacity_before);

    CHECK_THROWS_AS(static_cast<void>(buffer.prepare(900)), std::bad_alloc);

    armed = false;
    CHECK_EQ(buffer.prepare(900).size(), 900);
    CHECK(std::ranges::equal(buffer.data(), payload));
}

TEST_CASE("flat_buffer round-trips through prepare/commit/consume") {
    coio::flat_buffer buffer;
    const auto payload = make_payload(100);

    fill(buffer, payload);
    CHECK_EQ(buffer.size(), payload.size());
    CHECK(std::ranges::equal(buffer.data(), payload));

    buffer.consume(60);
    CHECK(std::ranges::equal(buffer.data(), std::span{payload}.subspan(60)));

    buffer.consume(999); // clamped to what is readable
    CHECK_EQ(buffer.size(), 0);
}
