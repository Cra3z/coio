#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <system_error>
#include <vector>
#include <doctest/doctest.h>
#include <coio/asyncio/io.h>
#include <coio/detail/error.h>
#include <coio/utils/async_result.h>
#include <coio/utils/flat_buffer.h>

namespace {
    auto make_payload(std::size_t size) -> std::vector<std::byte> {
        std::vector<std::byte> payload(size);
        for (std::size_t i = 0; i < size; ++i) {
            payload[i] = static_cast<std::byte>((i * 131 + 17) & 0xff);
        }
        return payload;
    }

    // Hands out at most `chunk` bytes per call and throws once the caller has taken `fail_after`
    // bytes in total, modelling a device that fails part-way through a complete-transfer loop.
    class flaky_source {
    public:
        flaky_source(std::vector<std::byte> payload, std::size_t chunk, std::size_t fail_after) noexcept
            : payload_(std::move(payload)), chunk_(chunk), fail_after_(fail_after) {}

        auto read_some(std::span<std::byte> buffer) -> std::size_t {
            largest_request_ = std::max(largest_request_, buffer.size());
            if (buffer.empty()) return 0;
            if (produced_ >= fail_after_) {
                throw std::system_error{std::make_error_code(std::errc::io_error), "flaky_source"};
            }
            const std::size_t n = std::min({chunk_, buffer.size(), fail_after_ - produced_, payload_.size() - produced_});
            std::ranges::copy(std::span{payload_}.subspan(produced_, n), buffer.begin());
            produced_ += n;
            return n;
        }

        auto read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t {
            // the complete-transfer loop must advance the offset by what it has already taken
            CHECK_EQ(offset, base_offset + produced_);
            return read_some(buffer);
        }

        [[nodiscard]]
        auto produced() const noexcept -> std::size_t {
            return produced_;
        }

        /// the largest single `prepare` the algorithm made the buffer perform
        [[nodiscard]]
        auto largest_request() const noexcept -> std::size_t {
            return largest_request_;
        }

        static constexpr std::size_t base_offset = 4096;

    private:
        std::vector<std::byte> payload_;
        std::size_t chunk_;
        std::size_t fail_after_;
        std::size_t produced_ = 0;
        std::size_t largest_request_ = 0;
    };

    // The async mirror of `flaky_source`. It reports failure through `set_error` rather than by
    // throwing, because the composed loop re-enters the device from a noexcept completion path.
    class async_flaky_source {
    public:
        using result_type = coio::async_result<
            coio::execution::set_value_t(std::size_t),
            coio::execution::set_error_t(std::error_code)
        >;

        async_flaky_source(
            std::vector<std::byte> payload,
            std::size_t chunk,
            std::size_t stop_after,
            std::error_code terminal_ec = std::make_error_code(std::errc::io_error)
        ) noexcept
            : payload_(std::move(payload)), chunk_(chunk), stop_after_(stop_after), terminal_ec_(terminal_ec) {}

        auto async_read_some(std::span<std::byte> buffer) noexcept -> result_type {
            ++calls_;
            largest_request_ = std::max(largest_request_, buffer.size());
            result_type result;
            if (buffer.empty()) {
                result.set_value(std::size_t{0});
                return result;
            }
            if (produced_ >= stop_after_) {
                result.set_error(terminal_ec_);
                return result;
            }
            const std::size_t n = std::min({chunk_, buffer.size(), stop_after_ - produced_, payload_.size() - produced_});
            std::ranges::copy(std::span{payload_}.subspan(produced_, n), buffer.begin());
            produced_ += n;
            result.set_value(n);
            return result;
        }

        [[nodiscard]]
        auto largest_request() const noexcept -> std::size_t {
            return largest_request_;
        }

        [[nodiscard]]
        auto calls() const noexcept -> std::size_t {
            return calls_;
        }

    private:
        std::vector<std::byte> payload_;
        std::size_t chunk_;
        std::size_t stop_after_;
        std::error_code terminal_ec_;
        std::size_t produced_ = 0;
        std::size_t largest_request_ = 0;
        std::size_t calls_ = 0;
    };

    // With an inline-completing device the whole loop runs inside `start()`, so these need no
    // context to drive them.
    struct completion_probe {
        using receiver_concept = coio::execution::receiver_tag;

        auto set_value(std::error_code ec, std::size_t n) && noexcept -> void {
            *out_ec = ec;
            *out_n = n;
        }

        std::error_code* out_ec;
        std::size_t* out_n;
    };

    // The mirror image: accepts at most `chunk` bytes per call, then throws once `fail_after`
    // bytes have been accepted.
    class flaky_sink {
    public:
        flaky_sink(std::size_t chunk, std::size_t fail_after) noexcept : chunk_(chunk), fail_after_(fail_after) {}

        auto write_some(std::span<const std::byte> buffer) -> std::size_t {
            if (buffer.empty()) return 0;
            if (written_.size() >= fail_after_) {
                throw std::system_error{std::make_error_code(std::errc::io_error), "flaky_sink"};
            }
            const std::size_t n = std::min({chunk_, buffer.size(), fail_after_ - written_.size()});
            written_.insert(written_.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
            return n;
        }

        auto write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t {
            CHECK_EQ(offset, base_offset + written_.size());
            return write_some(buffer);
        }

        [[nodiscard]]
        auto written() const noexcept -> std::span<const std::byte> {
            return written_;
        }

        static constexpr std::size_t base_offset = 4096;

    private:
        std::size_t chunk_;
        std::size_t fail_after_;
        std::vector<std::byte> written_;
    };

    // Refuses to allocate while armed, so a buffer that cannot grow mid-transfer can be staged.
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

    static_assert(coio::input_stream_device<flaky_source>);
    static_assert(coio::input_random_access_device<flaky_source>);
    static_assert(coio::output_stream_device<flaky_sink>);
    static_assert(coio::output_random_access_device<flaky_sink>);
    static_assert(coio::async_input_stream_device<async_flaky_source>);
    static_assert(coio::dynamic_buffer<coio::flat_buffer>);

    // the ceiling on any single `prepare` the algorithms perform — asio's default_max_transfer_size
    constexpr std::size_t max_chunk = 65536;

    // ...and the floor, which is also the size of the first chunk into an empty buffer
    constexpr std::size_t min_chunk = 512;
}

TEST_CASE("read into a dynamic buffer fills and commits the whole request") {
    const auto payload = make_payload(256);
    flaky_source source{payload, 40, 256};
    coio::flat_buffer buffer;

    CHECK_EQ(coio::read(source, buffer, payload.size()), payload.size());
    CHECK_EQ(buffer.size(), payload.size());
    CHECK(std::ranges::equal(buffer.data(), payload));
}

TEST_CASE("read into a dynamic buffer commits what arrived before the device threw") {
    const auto payload = make_payload(256);
    flaky_source source{payload, 40, 100};
    coio::flat_buffer buffer;

    CHECK_THROWS_AS(coio::read(source, buffer, payload.size()), std::system_error);

    CHECK_EQ(source.produced(), 100);
    CHECK_EQ(buffer.size(), 100);
    CHECK(std::ranges::equal(buffer.data(), std::span{payload}.first(100)));
}

TEST_CASE("read_at into a dynamic buffer commits what arrived before the device threw") {
    const auto payload = make_payload(256);
    flaky_source source{payload, 40, 100};
    coio::flat_buffer buffer;

    CHECK_THROWS_AS(coio::read_at(source, flaky_source::base_offset, buffer, payload.size()), std::system_error);

    CHECK_EQ(buffer.size(), 100);
    CHECK(std::ranges::equal(buffer.data(), std::span{payload}.first(100)));
}

TEST_CASE("write from a dynamic buffer consumes the whole readable region") {
    const auto payload = make_payload(256);
    flaky_sink sink{40, 256};
    coio::flat_buffer buffer;
    std::ranges::copy(payload, buffer.prepare(payload.size()).begin());
    buffer.commit(payload.size());

    CHECK_EQ(coio::write(sink, buffer), payload.size());
    CHECK_EQ(buffer.size(), 0);
    CHECK(std::ranges::equal(sink.written(), payload));
}

TEST_CASE("write from a dynamic buffer consumes exactly what reached the device before it threw") {
    const auto payload = make_payload(256);
    flaky_sink sink{40, 100};
    coio::flat_buffer buffer;
    std::ranges::copy(payload, buffer.prepare(payload.size()).begin());
    buffer.commit(payload.size());

    CHECK_THROWS_AS(coio::write(sink, buffer), std::system_error);

    CHECK_EQ(sink.written().size(), 100);
    CHECK(std::ranges::equal(sink.written(), std::span{payload}.first(100)));
    CHECK_EQ(buffer.size(), payload.size() - 100);
    CHECK(std::ranges::equal(buffer.data(), std::span{payload}.subspan(100)));
}

TEST_CASE("write_at from a dynamic buffer consumes exactly what reached the device before it threw") {
    const auto payload = make_payload(256);
    flaky_sink sink{40, 100};
    coio::flat_buffer buffer;
    std::ranges::copy(payload, buffer.prepare(payload.size()).begin());
    buffer.commit(payload.size());

    CHECK_THROWS_AS(coio::write_at(sink, flaky_sink::base_offset, buffer), std::system_error);

    CHECK_EQ(sink.written().size(), 100);
    CHECK_EQ(buffer.size(), payload.size() - 100);
    CHECK(std::ranges::equal(buffer.data(), std::span{payload}.subspan(100)));
}

// The point of the chunked loop: `total` is routinely a number the peer chose (an HTTP
// Content-Length, a length prefix), and it must cost memory only as the bytes actually turn up.
TEST_CASE("read into a dynamic buffer grows with the data, not with the request") {
    const auto payload = make_payload(4096);
    flaky_source source{payload, 64, 64}; // one 64-byte chunk, then the device dies
    coio::flat_buffer buffer;

    CHECK_THROWS_AS(coio::read(source, buffer, std::size_t{1} << 30), std::system_error);

    CHECK_EQ(buffer.size(), 64);                     // what arrived is committed
    CHECK_LE(source.largest_request(), max_chunk);   // the gigabyte was never asked for
    CHECK_LE(buffer.capacity(), max_chunk);          // ...and never allocated
}

TEST_CASE("async_read into a dynamic buffer grows with the data, not with the request") {
    const auto payload = make_payload(4096);
    async_flaky_source source{payload, 64, 64};
    coio::flat_buffer buffer;

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(
        coio::async_read(source, buffer, std::size_t{1} << 30),
        completion_probe{&ec, &n}
    );
    coio::execution::start(op);

    CHECK_EQ(ec, std::errc::io_error);
    CHECK_EQ(n, 64);
    CHECK_EQ(buffer.size(), 64);
    CHECK_LE(source.largest_request(), max_chunk);
    CHECK_LE(buffer.capacity(), max_chunk);
}

// A request no amount of data could satisfy is a programming error: it is reported from the
// initiating call, before the device is touched and before anything is allocated.
TEST_CASE("a transfer larger than the buffer can ever hold is rejected before any of it is read") {
    const auto payload = make_payload(4096);

    SUBCASE("read") {
        flaky_source source{payload, 64, 4096};
        coio::flat_buffer buffer{1024}; // max_size == 1024
        // the synchronous form reports by throwing, but carries the code its asynchronous
        // counterpart completes with
        try {
            static_cast<void>(coio::read(source, buffer, 2048));
            FAIL("expected std::system_error");
        }
        catch (const std::system_error& e) {
            CHECK_EQ(e.code(), std::errc::no_buffer_space);
        }
        CHECK_EQ(buffer.size(), 0);
        CHECK_EQ(buffer.capacity(), 0);
        CHECK_EQ(source.largest_request(), 0);
    }

    // the asynchronous one has an error channel, so nothing escapes the initiating call
    SUBCASE("async_read") {
        async_flaky_source source{payload, 64, 4096};
        coio::flat_buffer buffer{1024};

        std::error_code ec;
        std::size_t n = 0;
        auto op = coio::execution::connect(coio::async_read(source, buffer, 2048), completion_probe{&ec, &n});
        coio::execution::start(op);

        CHECK_EQ(ec, std::errc::no_buffer_space);
        CHECK_EQ(n, 0);
        CHECK_EQ(buffer.size(), 0);
        CHECK_EQ(buffer.capacity(), 0);
    }
}

TEST_CASE("async_read into a dynamic buffer commits every chunk as it lands") {
    const auto payload = make_payload(4096);
    async_flaky_source source{payload, 64, 4096};
    coio::flat_buffer buffer;

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(
        coio::async_read(source, buffer, payload.size()),
        completion_probe{&ec, &n}
    );
    coio::execution::start(op);

    CHECK_FALSE(ec);
    CHECK_EQ(n, payload.size());
    CHECK_EQ(buffer.size(), payload.size());
    CHECK(std::ranges::equal(buffer.data(), payload));
}

TEST_CASE("async_read without a size reads until the stream ends") {
    const auto payload = make_payload(4096);
    async_flaky_source source{payload, 700, payload.size(), make_error_code(coio::error::eof)};
    coio::flat_buffer buffer;

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(coio::async_read(source, buffer), completion_probe{&ec, &n});
    coio::execution::start(op);

    CHECK_EQ(ec, coio::error::eof);
    CHECK_EQ(n, payload.size());
    CHECK_EQ(buffer.size(), payload.size());
    CHECK(std::ranges::equal(buffer.data(), payload));
    CHECK_LE(source.largest_request(), max_chunk);
}

TEST_CASE("async_read without a size stops when the buffer is full, and says so by not erroring") {
    const auto payload = make_payload(4096);
    async_flaky_source source{payload, 700, payload.size(), make_error_code(coio::error::eof)};
    coio::flat_buffer buffer{1024}; // max_size == 1024

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(coio::async_read(source, buffer), completion_probe{&ec, &n});
    coio::execution::start(op);

    CHECK_FALSE(ec); // no eof: the stream still has data, the buffer just cannot take it
    CHECK_EQ(n, 1024);
    CHECK_EQ(buffer.size(), buffer.max_size());
    CHECK(std::ranges::equal(buffer.data(), std::span{payload}.first(1024)));
}

// The reason `dynamic_buffer` requires the error-code overload at all.
TEST_CASE("async_read reports a buffer that cannot grow mid-transfer") {
    const auto payload = make_payload(4096);
    async_flaky_source source{payload, 64, payload.size()};
    bool armed = false;
    fallible_buffer buffer{std::size_t{1} << 20, failing_allocator<std::byte>{&armed}};
    // The sender prepares nothing until it is started, so the window the old test used — between
    // the initiating call and `start()` — no longer exists. Instead, give the buffer room for the
    // first chunk up front; that one then needs no allocation, and the first prepare that does is
    // the one the loop makes from inside a completion.
    buffer.reserve(min_chunk);

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(coio::async_read(source, buffer, 4096), completion_probe{&ec, &n});
    armed = true; // from here every attempt to grow the buffer fails
    coio::execution::start(op);

    CHECK_EQ(ec, std::errc::not_enough_memory);
    CHECK_GT(n, 0);              // the chunks that did arrive are accounted for
    CHECK_EQ(buffer.size(), n);  // ...and committed
}

TEST_CASE("async_read_until reports a buffer that cannot grow") {
    async_flaky_source source{make_payload(4096), 64, 4096};
    bool armed = true; // read_until prepares from start(), which is already the noexcept path
    fallible_buffer buffer{std::size_t{1} << 20, failing_allocator<std::byte>{&armed}};

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(coio::async_read_until(source, buffer, '\n'), completion_probe{&ec, &n});
    coio::execution::start(op);

    CHECK_EQ(ec, std::errc::not_enough_memory);
    CHECK_EQ(n, 0);
}

// Reading into a dynamic buffer is lazy: composing the sender, and even connecting it, leaves the
// buffer exactly as it was. `prepare` grows the buffer and can fail, so it waits for `start()`.
// (`async_write` is not deferred — it only reads `data()`, which changes nothing.)
TEST_CASE("async_read prepares its first chunk from start(), not from the initiating call") {
    async_flaky_source source{make_payload(4096), 64, 4096};
    coio::flat_buffer buffer;

    auto sndr = coio::async_read(source, buffer, 128);
    CHECK_EQ(buffer.capacity(), 0); // nothing was prepared
    CHECK_EQ(source.calls(), 0);    // and no device operation was built

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(std::move(sndr), completion_probe{&ec, &n});
    CHECK_EQ(buffer.capacity(), 0); // connecting is not starting
    CHECK_EQ(source.calls(), 0);

    coio::execution::start(op);

    CHECK_FALSE(ec);
    CHECK_EQ(n, 128);
    CHECK_EQ(buffer.size(), 128);
}

// A request the buffer already satisfies still goes to the device — as a zero-length read — rather
// than completing on the spot. That is what keeps every outcome arriving where the device
// completes rather than on the thread that called `start()`, and it is what asio's loop does too.
TEST_CASE("a dynamic-buffer read that is already satisfied still completes through the device") {
    const auto payload = make_payload(4096);

    SUBCASE("nothing was asked for") {
        async_flaky_source source{payload, 64, payload.size()};
        coio::flat_buffer buffer;

        std::error_code ec;
        std::size_t n = 0;
        auto op = coio::execution::connect(coio::async_read(source, buffer, 0), completion_probe{&ec, &n});
        coio::execution::start(op);

        CHECK_FALSE(ec);
        CHECK_EQ(n, 0);
        CHECK_EQ(source.calls(), 1);
        CHECK_EQ(source.largest_request(), 0); // the one call was zero-length
    }

    SUBCASE("the buffer is already full") {
        async_flaky_source source{payload, 64, payload.size()};
        coio::flat_buffer buffer{16};
        std::ranges::copy(std::span{payload}.first(16), buffer.prepare(16).begin());
        buffer.commit(16);
        REQUIRE_EQ(buffer.size(), buffer.max_size());

        std::error_code ec;
        std::size_t n = 0;
        auto op = coio::execution::connect(coio::async_read(source, buffer), completion_probe{&ec, &n});
        coio::execution::start(op);

        CHECK_FALSE(ec); // no error is how "the buffer is full" is spelled here
        CHECK_EQ(n, 0);
        CHECK_EQ(source.calls(), 1);
        CHECK_EQ(source.largest_request(), 0);
        CHECK_EQ(buffer.size(), 16); // untouched
    }
}

// The sizeless form used to prepare its first chunk through the throwing overload, so an
// allocation failure escaped the initiating call while its sized sibling reported one.
TEST_CASE("async_read without a size reports a first chunk it cannot allocate") {
    async_flaky_source source{make_payload(4096), 64, 4096};
    bool armed = true;
    fallible_buffer buffer{std::size_t{1} << 20, failing_allocator<std::byte>{&armed}};

    std::error_code ec;
    std::size_t n = 0;
    auto op = coio::execution::connect(coio::async_read(source, buffer), completion_probe{&ec, &n});
    coio::execution::start(op);

    CHECK_EQ(ec, std::errc::not_enough_memory);
    CHECK_EQ(n, 0);
}

TEST_CASE("complete-transfer algorithms accept empty requests without touching the device") {
    flaky_source source{{}, 40, 0}; // any read at all throws
    flaky_sink sink{40, 0};         // any write at all throws
    coio::flat_buffer buffer;

    CHECK_EQ(coio::read(source, std::span<std::byte>{}), 0);
    CHECK_EQ(coio::write(sink, std::span<const std::byte>{}), 0);
    CHECK_EQ(coio::read(source, buffer, 0), 0);
    CHECK_EQ(coio::write(sink, buffer), 0);
    CHECK_EQ(buffer.size(), 0);
}
