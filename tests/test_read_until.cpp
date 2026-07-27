#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <doctest/doctest.h>
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/detail/error.h>
#include <coio/utils/flat_buffer.h>

namespace {
    // Serves scripted chunks; each read_some transfers at most the remainder of the current chunk.
    class scripted_device {
    public:
        explicit scripted_device(std::vector<std::string> chunks) : chunks_(std::move(chunks)) {}

        auto read_some(std::span<std::byte> buffer) -> std::size_t {
            if (buffer.empty()) return 0;
            if (chunk_index_ == chunks_.size()) {
                throw std::system_error{coio::error::eof, "read_some"};
            }
            const std::string& chunk = chunks_[chunk_index_];
            const std::size_t n = std::min(buffer.size(), chunk.size() - offset_);
            std::memcpy(buffer.data(), chunk.data() + offset_, n);
            offset_ += n;
            if (offset_ == chunk.size()) {
                ++chunk_index_;
                offset_ = 0;
            }
            return n;
        }

        auto async_read_some(std::span<std::byte> buffer) {
            return coio::just(read_some(buffer));
        }

    private:
        std::vector<std::string> chunks_;
        std::size_t chunk_index_ = 0;
        std::size_t offset_ = 0;
    };

    // Produces the same non-delimiter byte forever, always filling the whole span.
    class repeating_device {
    public:
        explicit repeating_device(char fill) : fill_(fill) {}

        auto read_some(std::span<std::byte> buffer) -> std::size_t {
            std::ranges::fill(buffer, std::byte(fill_));
            return buffer.size();
        }

        auto async_read_some(std::span<std::byte> buffer) {
            return coio::just(read_some(buffer));
        }

    private:
        char fill_;
    };

    static_assert(coio::input_stream_device<scripted_device>);
    static_assert(coio::async_input_stream_device<scripted_device>);
    static_assert(coio::input_stream_device<repeating_device>);
    static_assert(coio::async_input_stream_device<repeating_device>);

    auto as_string(std::span<const std::byte> bytes) -> std::string_view {
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }
}

TEST_CASE("read_until returns count including delimiter and keeps excess in the buffer") {
    SUBCASE("char delimiter") {
        scripted_device device{{"hello\nworld"}};
        coio::flat_buffer buffer;
        CHECK_EQ(coio::read_until(device, buffer, '\n'), 6);
        CHECK_EQ(as_string(buffer.data()), "hello\nworld");
        buffer.consume(6);
        CHECK_EQ(as_string(buffer.data()), "world");
    }
    SUBCASE("string delimiter") {
        scripted_device device{{"ab--cd"}};
        coio::flat_buffer buffer;
        CHECK_EQ(coio::read_until(device, buffer, std::string_view{"--"}), 4);
        CHECK_EQ(as_string(buffer.data()), "ab--cd");
        buffer.consume(4);
        CHECK_EQ(as_string(buffer.data()), "cd");
    }
}

TEST_CASE("read_until throws not_found when the buffer reaches max_size without a delimiter") {
    SUBCASE("char delimiter") {
        repeating_device device{'a'};
        coio::flat_buffer buffer{8};
        try {
            (void) coio::read_until(device, buffer, '\n');
            FAIL("expected std::system_error");
        }
        catch (const std::system_error& e) {
            CHECK_EQ(e.code(), coio::error::not_found);
        }
        CHECK_EQ(buffer.size(), buffer.max_size());
        CHECK_EQ(as_string(buffer.data()), "aaaaaaaa");
    }
    SUBCASE("string delimiter") {
        repeating_device device{'a'};
        coio::flat_buffer buffer{8};
        try {
            (void) coio::read_until(device, buffer, std::string_view{"\r\n"});
            FAIL("expected std::system_error");
        }
        catch (const std::system_error& e) {
            CHECK_EQ(e.code(), coio::error::not_found);
        }
        CHECK_EQ(buffer.size(), buffer.max_size());
        CHECK_EQ(as_string(buffer.data()), "aaaaaaaa");
    }
}

TEST_CASE("read_until succeeds when the delimiter arrives exactly at max_size") {
    SUBCASE("char delimiter") {
        scripted_device device{{"abcdefg\n"}};
        coio::flat_buffer buffer{8};
        CHECK_EQ(coio::read_until(device, buffer, '\n'), 8);
        CHECK_EQ(as_string(buffer.data()), "abcdefg\n");
    }
    SUBCASE("string delimiter") {
        scripted_device device{{"abcdef\r\n"}};
        coio::flat_buffer buffer{8};
        CHECK_EQ(coio::read_until(device, buffer, std::string_view{"\r\n"}), 8);
        CHECK_EQ(as_string(buffer.data()), "abcdef\r\n");
    }
}

TEST_CASE("read_until finds a multi-char delimiter spanning two reads") {
    scripted_device device{{"abc\r", "\nrest"}};
    coio::flat_buffer buffer;
    CHECK_EQ(coio::read_until(device, buffer, std::string_view{"\r\n"}), 5);
    CHECK_EQ(as_string(buffer.data()), "abc\r\nrest");
    buffer.consume(5);
    CHECK_EQ(as_string(buffer.data()), "rest");
}

TEST_CASE("read_until with an empty string delimiter returns 0 without reading") {
    scripted_device device{{}};
    coio::flat_buffer buffer;
    CHECK_EQ(coio::read_until(device, buffer, std::string_view{}), 0);
    CHECK_EQ(buffer.size(), 0);
}

TEST_CASE("async_read_until completes with count including delimiter and keeps excess") {
    scripted_device device{{"hello\nworld"}};
    coio::flat_buffer buffer;
    auto result = coio::this_thread::sync_wait(coio::async_read_until(device, buffer, '\n'));
    REQUIRE(result.has_value());
    auto [ec, n] = result.value();
    CHECK_FALSE(ec);
    CHECK_EQ(n, 6);
    CHECK_EQ(as_string(buffer.data()), "hello\nworld");
}

TEST_CASE("async_read_until completes with not_found when the buffer reaches max_size") {
    SUBCASE("char delimiter") {
        repeating_device device{'a'};
        coio::flat_buffer buffer{8};
        auto result = coio::this_thread::sync_wait(coio::async_read_until(device, buffer, '\n'));
        REQUIRE(result.has_value());
        auto [ec, n] = result.value();
        CHECK_EQ(ec, coio::error::not_found);
        CHECK_EQ(n, 0);
        CHECK_EQ(buffer.size(), buffer.max_size());
        CHECK_EQ(as_string(buffer.data()), "aaaaaaaa");
    }
    SUBCASE("string delimiter") {
        repeating_device device{'a'};
        coio::flat_buffer buffer{8};
        auto result = coio::this_thread::sync_wait(coio::async_read_until(device, buffer, std::string_view{"\r\n"}));
        REQUIRE(result.has_value());
        auto [ec, n] = result.value();
        CHECK_EQ(ec, coio::error::not_found);
        CHECK_EQ(n, 0);
        CHECK_EQ(buffer.size(), buffer.max_size());
        CHECK_EQ(as_string(buffer.data()), "aaaaaaaa");
    }
}

TEST_CASE("async_read_until succeeds when the delimiter arrives exactly at max_size") {
    scripted_device device{{"abcdefg\n"}};
    coio::flat_buffer buffer{8};
    auto result = coio::this_thread::sync_wait(coio::async_read_until(device, buffer, '\n'));
    REQUIRE(result.has_value());
    auto [ec, n] = result.value();
    CHECK_FALSE(ec);
    CHECK_EQ(n, 8);
    CHECK_EQ(as_string(buffer.data()), "abcdefg\n");
}

TEST_CASE("async_read_until finds a multi-char delimiter spanning two reads") {
    scripted_device device{{"abc\r", "\nrest"}};
    coio::flat_buffer buffer;
    auto result = coio::this_thread::sync_wait(coio::async_read_until(device, buffer, std::string_view{"\r\n"}));
    REQUIRE(result.has_value());
    auto [ec, n] = result.value();
    CHECK_FALSE(ec);
    CHECK_EQ(n, 5);
    CHECK_EQ(as_string(buffer.data()), "abc\r\nrest");
}

TEST_CASE("async_read_until with an empty string delimiter completes with 0") {
    scripted_device device{{}};
    coio::flat_buffer buffer;
    auto result = coio::this_thread::sync_wait(coio::async_read_until(device, buffer, std::string_view{}));
    REQUIRE(result.has_value());
    auto [ec, n] = result.value();
    CHECK_FALSE(ec);
    CHECK_EQ(n, 0);
    CHECK_EQ(buffer.size(), 0);
}
