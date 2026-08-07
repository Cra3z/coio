#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>
#include <doctest/doctest.h>
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/pipe.h>
#include <coio/detail/config.h>
#include <coio/detail/error.h>
#include <coio/utils/scope_exit.h>

#if COIO_OS_LINUX
#include <csignal>
#include <coio/asyncio/epoll_context.h>
#if COIO_HAS_IO_URING
#include <coio/asyncio/uring_context.h>
#endif
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
#endif

// Register readable type names so templated test-case names embed the context type
// (needed for doctest's --test-case-exclude, e.g. excluding "*uring_context*" under TSan).
#if COIO_OS_LINUX
TYPE_TO_STRING(coio::epoll_context);
#if COIO_HAS_IO_URING
TYPE_TO_STRING(coio::uring_context);
#endif
#elif COIO_OS_WINDOWS
TYPE_TO_STRING(coio::iocp_context);
#endif

// the pipe suite is parameterized exactly like the socket suite.
#if COIO_OS_LINUX and COIO_HAS_IO_URING
#define COIO_TEST_CONTEXTS coio::epoll_context, coio::uring_context
#elif COIO_OS_LINUX
#define COIO_TEST_CONTEXTS coio::epoll_context
#else
#define COIO_TEST_CONTEXTS coio::iocp_context
#endif

using namespace std::chrono_literals;

namespace {
    template<typename Scheduler>
    using pipe_reader_t = coio::pipe_reader<Scheduler>;
    template<typename Scheduler>
    using pipe_writer_t = coio::pipe_writer<Scheduler>;

    // Constructing a context can fail at runtime (io_uring may be unavailable or disabled via
    // seccomp / kernel.io_uring_disabled); treat that as a skip, not a failure.
    template<typename Context>
    auto try_make_context(std::optional<Context>& context) -> bool {
        try {
            context.emplace();
            return true;
        }
        catch (const std::system_error& e) {
            MESSAGE("skipping: cannot construct context: " << e.what());
            return false;
        }
    }

    // The canonical driving pattern: the driver task calls ctx.run() as one branch of a
    // when_all raced via sync_wait; the I/O tasks are started on the context's scheduler.
    template<typename Context>
    auto drive(Context& context) -> coio::task<> {
        context.run();
        co_return;
    }

    auto as_string(std::span<const char> chars) -> std::string_view {
        return {chars.data(), chars.size()};
    }

    auto make_payload(std::size_t size, std::uint8_t salt) -> std::vector<std::byte> {
        std::vector<std::byte> payload(size);
        for (std::size_t i = 0; i < size; ++i) {
            payload[i] = static_cast<std::byte>((i * 131 + 17 + salt) & 0xff);
        }
        return payload;
    }

    // --- transfer helpers ------------------------------------------------------

    template<typename Scheduler>
    auto write_all(pipe_writer_t<Scheduler>& writer, std::span<const std::byte> payload) -> coio::task<> {
        auto [ec, n] = co_await coio::async_write(writer, payload);
        CHECK_FALSE(ec);
        CHECK_EQ(n, payload.size());
    }

    template<typename Scheduler>
    auto read_exact(pipe_reader_t<Scheduler>& reader, std::span<std::byte> dest) -> coio::task<> {
        auto [ec, n] = co_await coio::async_read(reader, dest);
        CHECK_FALSE(ec);
        CHECK_EQ(n, dest.size());
    }

    // --- writer-close/EOF helpers ----------------------------------------------

    template<typename Scheduler>
    auto write_then_close(pipe_writer_t<Scheduler>& writer, std::span<const std::byte> payload) -> coio::task<> {
        auto [ec, n] = co_await coio::async_write(writer, payload);
        CHECK_FALSE(ec);
        CHECK_EQ(n, payload.size());
        writer.close(); // data already buffered in the pipe stays readable
        CHECK_FALSE(writer.is_open());
    }

    template<typename Scheduler>
    auto drain_then_expect_eof(pipe_reader_t<Scheduler>& reader, std::span<const std::byte> expected) -> coio::task<> {
        std::vector<std::byte> buffer(expected.size());
        auto [ec, n] = co_await coio::async_read(reader, buffer);
        CHECK_FALSE(ec);
        CHECK_EQ(n, expected.size());
        CHECK(std::ranges::equal(buffer, expected));

        // buffered data drained and the writer is gone: the next read reports eof
        // (Windows surfaces ERROR_BROKEN_PIPE, which coio maps to the same coio::error::eof)
        char tail[16];
        try {
            (void) co_await reader.async_read_some(coio::as_writable_bytes(tail));
            FAIL("expected coio::error::eof after the writer closed");
        }
        catch (const std::system_error& e) {
            CHECK_EQ(e.code(), coio::error::eof);
        }
    }

    // --- broken-pipe helper ----------------------------------------------------

    template<typename Scheduler>
    auto write_until_broken(pipe_writer_t<Scheduler>& writer) -> coio::task<> {
        // the pipe buffer is finite (4096 bytes for the Windows named-pipe pair), so with
        // the reader gone the writes must start failing; bound the loop so a regression
        // fails instead of spinning
        const std::vector<std::byte> chunk(4096, std::byte{0x42});
        for (int i = 0; i < 256; ++i) {
            try {
                (void) co_await writer.async_write_some(coio::as_bytes(chunk));
            }
            catch (const std::system_error& e) {
#if COIO_OS_WINDOWS
                // The client end of a named pipe whose server (the reader) closed fails
                // WriteFile with ERROR_NO_DATA (232, "The pipe is being closed"), which
                // MSVC's system_category does NOT map to std::errc::broken_pipe (a write
                // already pending at close time gets ERROR_BROKEN_PIPE, 109, instead).
                // Accept Windows' broken-pipe spellings rather than the generic condition.
                const bool broken_pipe = e.code() == std::errc::broken_pipe
                    or e.code() == std::error_code{232 /*ERROR_NO_DATA*/, std::system_category()}
                    or e.code() == std::error_code{109 /*ERROR_BROKEN_PIPE*/, std::system_category()};
                CHECK_MESSAGE(broken_pipe, "unexpected error: ", e.what());
#else
                CHECK_EQ(e.code(), std::errc::broken_pipe);
#endif
                co_return;
            }
        }
        FAIL("writes kept succeeding long after the reader closed");
    }

    // --- cancellation helper ---------------------------------------------------

    // Races a pending read on an empty pipe against a short timer; the timer must win
    // and when_any must cancel the read (when_any only completes after all children did).
    template<typename Scheduler>
    auto cancel_pending_read(pipe_reader_t<Scheduler>& reader, Scheduler scheduler) -> coio::task<> {
        char buffer[16];
        const int winner = co_await coio::when_any(
            reader.async_read_some(coio::as_writable_bytes(buffer)) | coio::then([](std::size_t) { return 1; }),
            scheduler.schedule_after(100ms) | coio::then([] { return 2; })
        );
        CHECK_EQ(winner, 2); // the timer won: the pending read was stopped
    }

    // --- zero-length helper ----------------------------------------------------

    template<typename Scheduler>
    auto zero_length_ops(pipe_reader_t<Scheduler>& reader, pipe_writer_t<Scheduler>& writer, Scheduler scheduler) -> coio::task<> {
        // the sharp edge of the zero-length convention: an empty read on an EMPTY pipe
        // must complete immediately with 0 rather than wait for data; race a generous
        // timer so a regression fails visibly instead of deadlocking the case
        const int winner = co_await coio::when_any(
            reader.async_read_some({}) | coio::then([](std::size_t n) {
                CHECK_EQ(n, 0);
                return 1;
            }),
            scheduler.schedule_after(5s) | coio::then([] { return 2; })
        );
        CHECK_EQ(winner, 1); // the empty read completed first, with 0

        const std::size_t written = co_await writer.async_write_some({});
        CHECK_EQ(written, 0);
    }

    // --- make_pipe overload helper ---------------------------------------------

    // one byte through the pair, synchronously (sync members never touch the completion
    // queue, so no running driver is needed)
    template<typename Reader, typename Writer>
    auto one_byte_through(Reader& reader, Writer& writer) -> void {
        REQUIRE(reader.is_open());
        REQUIRE(writer.is_open());
        const std::byte out{0x5a};
        CHECK_EQ(writer.write_some(std::span{&out, 1}), 1);
        std::byte in{};
        CHECK_EQ(reader.read_some(std::span{&in, 1}), 1);
        CHECK_EQ(int(in), int(out));
    }
}

TEST_CASE_TEMPLATE("pipe: async write/read roundtrip", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    auto [reader, writer] = coio::make_pipe(scheduler);
    static constexpr std::string_view message = "hello through the pipe";
    std::vector<char> received(message.size());

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, read_exact(reader, coio::as_writable_bytes(received))),
        coio::starts_on(scheduler, write_all(writer, coio::as_bytes(message))),
        drive(*context)
    ));

    CHECK_EQ(as_string(received), message);
}

TEST_CASE_TEMPLATE("pipe: closing the writer surfaces eof after the data drains", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    auto [reader, writer] = coio::make_pipe(scheduler);
    const auto payload = make_payload(64, 1);

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, drain_then_expect_eof(reader, coio::as_bytes(payload))),
        coio::starts_on(scheduler, write_then_close(writer, coio::as_bytes(payload))),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("pipe: writing after the reader closes reports broken pipe", Context, COIO_TEST_CONTEXTS) {
#if COIO_OS_LINUX
    const auto previous_handler = std::signal(SIGPIPE, SIG_IGN);
    REQUIRE(previous_handler != SIG_ERR);
    coio::scope_exit restore_sigpipe{[previous_handler]() noexcept {
        std::signal(SIGPIPE, previous_handler);
    }};
#endif

    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    auto [reader, writer] = coio::make_pipe(scheduler);
    reader.close(); // deterministic: the read end is gone before the writer starts
    CHECK_FALSE(reader.is_open());

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, write_until_broken(writer)),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("pipe: large transfer with backpressure preserves the byte stream", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    auto [reader, writer] = coio::make_pipe(scheduler);

    // far beyond any pipe buffer (Windows uses 4096-byte buffers): the writer must block
    // on backpressure repeatedly while the reader concurrently drains
    constexpr std::size_t total = std::size_t{1} << 20; // 1 MiB
    const auto payload = make_payload(total, 2);
    std::vector<std::byte> received(total);

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, read_exact(reader, received)),
        coio::starts_on(scheduler, write_all(writer, payload)),
        drive(*context)
    ));

    CHECK(received == payload);
}

TEST_CASE_TEMPLATE("pipe: pending read on an empty pipe is cancelled by a timer race", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    auto [reader, writer] = coio::make_pipe(scheduler);
    // the writer end stays open and silent, so the read can only finish via cancellation

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, cancel_pending_read(reader, scheduler)),
        drive(*context)
    ));
} // context must destruct cleanly here: the raced read completed (stopped)

TEST_CASE_TEMPLATE("pipe: zero-length reads and writes complete immediately with 0", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    auto [reader, writer] = coio::make_pipe(scheduler);

    // sync forms first: the pipe is empty, yet neither call may block or report eof
    CHECK_EQ(reader.read_some(std::span<std::byte>{}), 0);
    CHECK_EQ(writer.write_some(std::span<const std::byte>{}), 0);

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, zero_length_ops(reader, writer, scheduler)),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("pipe: every make_pipe overload builds a working pair", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    // a second context of the same type for the dual-scheduler overloads (the two ends
    // may be bound to different execution contexts)
    std::optional<Context> other_context;
    if (not try_make_context(other_context)) return;
    auto other_scheduler = other_context->get_scheduler();

    { // make_pipe(sched): fresh OS pipe, both ends on one scheduler
        auto [reader, writer] = coio::make_pipe(scheduler);
        one_byte_through(reader, writer);
    }
    { // make_pipe(reader_sched, writer_sched): fresh OS pipe, one scheduler per end
        auto [reader, writer] = coio::make_pipe(scheduler, other_scheduler);
        one_byte_through(reader, writer);
    }
    { // make_pipe(sched, reader_h, writer_h): adopt existing native handles
        // (detail::make_native_pipe is the only portable way to obtain handles that are
        // valid for adoption — on Windows they must be overlapped-capable)
        auto [reader_handle, writer_handle] = coio::detail::make_native_pipe();
        auto [reader, writer] = coio::make_pipe(scheduler, reader_handle, writer_handle);
        one_byte_through(reader, writer);
    }
    { // make_pipe(sched1, reader_h, sched2, writer_h): adopt handles, one scheduler per end
        auto [reader_handle, writer_handle] = coio::detail::make_native_pipe();
        auto [reader, writer] = coio::make_pipe(scheduler, reader_handle, other_scheduler, writer_handle);
        one_byte_through(reader, writer);
    }
}
