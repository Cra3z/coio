#include <algorithm>
#include <cstddef>
#include <istream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <doctest/doctest.h>
#include <coio/asyncio/io.h>
#include <coio/utils/streambuf.h>

namespace {
    auto fill(coio::streambuf& buf, std::string_view bytes) -> void {
        auto prepared = buf.prepare(bytes.size());
        std::ranges::copy(std::as_bytes(std::span{bytes}), prepared.begin());
        buf.commit(bytes.size());
    }

    auto readable(const coio::streambuf& buf) -> std::string_view {
        const auto data = buf.data();
        return {reinterpret_cast<const char*>(data.data()), data.size()};
    }

    // Distinguishable content: a run of identical bytes would survive a wrong shift unnoticed.
    auto pattern(std::size_t size) -> std::string {
        std::string result(size, '\0');
        for (std::size_t i = 0; i < size; ++i) {
            result[i] = static_cast<char>('A' + i % 26);
        }
        return result;
    }

    static_assert(coio::dynamic_buffer<coio::streambuf>);
}

TEST_CASE("streambuf round-trips through prepare/commit/consume") {
    coio::streambuf buf;
    CHECK_EQ(buf.size(), 0);

    fill(buf, "hello world");
    CHECK_EQ(buf.size(), 11);
    CHECK_EQ(readable(buf), "hello world");

    buf.consume(6);
    CHECK_EQ(buf.size(), 5);
    CHECK_EQ(readable(buf), "world");

    buf.consume(99); // clamped to what is readable
    CHECK_EQ(buf.size(), 0);
}

// The shift and the growth have to agree about where the data ended up.
TEST_CASE("streambuf keeps its contents across a compacting prepare") {
    coio::streambuf buf;
    const std::string payload = pattern(100);
    fill(buf, payload);
    buf.consume(60);

    const std::string expected = payload.substr(60);
    REQUIRE_EQ(readable(buf), expected);

    // more than the free tail, so the storage both compacts and grows
    static_cast<void>(buf.prepare(100));

    CHECK_EQ(buf.size(), 40);
    CHECK_EQ(readable(buf), expected);
}

TEST_CASE("streambuf is unchanged when prepare exceeds max_size") {
    coio::streambuf buf{64};
    const std::string payload = pattern(40);
    fill(buf, payload);
    buf.consume(10); // gptr() is now off the front, so a prepare would want to compact

    const std::string expected = payload.substr(10);
    REQUIRE_EQ(readable(buf), expected);
    const std::size_t capacity_before = buf.capacity();

    // 30 readable + 40 more cannot fit in max_size() == 64
    CHECK_THROWS_AS(static_cast<void>(buf.prepare(40)), std::length_error);

    CHECK_EQ(buf.size(), 30);
    CHECK_EQ(readable(buf), expected);
    CHECK_EQ(buf.capacity(), capacity_before);

    fill(buf, "!");
    CHECK_EQ(readable(buf), expected + "!");
}

// The throwing overload is a wrapper over this one, so the two cannot disagree.
TEST_CASE("streambuf reports through the error code what the throwing prepare throws for") {
    coio::streambuf buf{64};
    const std::string payload = pattern(40);
    fill(buf, payload);
    buf.consume(10);

    const std::string expected = payload.substr(10);
    std::error_code ec;

    const auto prepared = buf.prepare(40, ec); // 30 readable + 40 > max_size() == 64
    CHECK_EQ(ec, std::errc::no_buffer_space);
    CHECK(prepared.empty());
    CHECK_EQ(readable(buf), expected);
    CHECK_THROWS_AS(static_cast<void>(buf.prepare(40)), std::length_error);

    const auto fits = buf.prepare(30, ec);
    CHECK_FALSE(ec);
    CHECK_EQ(fits.size(), 30);
    CHECK_EQ(readable(buf), expected);
}

TEST_CASE("streambuf works as the std::streambuf it derives from") {
    coio::streambuf buf;
    fill(buf, "first line\nsecond line\n");

    std::istream stream(&buf);
    std::string line;
    REQUIRE(std::getline(stream, line));
    CHECK_EQ(line, "first line");
    REQUIRE(std::getline(stream, line));
    CHECK_EQ(line, "second line");

    // what the istream consumed is gone from the readable region
    CHECK_EQ(buf.size(), 0);
}
