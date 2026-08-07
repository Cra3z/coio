#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>
#include <doctest/doctest.h>
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/asyncio/file.h>
#include <coio/detail/config.h>
#include <coio/detail/error.h>
#include <coio/utils/scope_exit.h>

#if COIO_OS_LINUX
#include <unistd.h>
#include <coio/asyncio/epoll_context.h> // only for the regular-file rejection case below
#if COIO_HAS_IO_URING
#include <coio/asyncio/uring_context.h>
#endif
#elif COIO_OS_WINDOWS
#include <process.h>
#include <coio/asyncio/iocp_context.h>
#endif

// Register readable type names so templated test-case names embed the context type
// (needed for doctest's --test-case-exclude, e.g. excluding "*uring_context*" under TSan).
#if COIO_OS_LINUX and COIO_HAS_IO_URING
TYPE_TO_STRING(coio::uring_context);
#elif COIO_OS_WINDOWS
TYPE_TO_STRING(coio::iocp_context);
#endif

#if COIO_OS_LINUX and COIO_HAS_IO_URING
#define COIO_FILE_TEST_CONTEXTS coio::uring_context
#elif COIO_OS_WINDOWS
#define COIO_FILE_TEST_CONTEXTS coio::iocp_context
#endif

namespace {
    template<typename Scheduler>
    using stream_file_t = coio::stream_file<Scheduler>;
    template<typename Scheduler>
    using random_access_file_t = coio::random_access_file<Scheduler>;

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

    template<typename Context>
    auto context_tag() -> std::string_view {
#if COIO_OS_LINUX
        if constexpr (std::is_same_v<Context, coio::epoll_context>) return "epoll";
#if COIO_HAS_IO_URING
        else if constexpr (std::is_same_v<Context, coio::uring_context>) return "uring";
#endif
        else return "ctx";
#elif COIO_OS_WINDOWS
        if constexpr (std::is_same_v<Context, coio::iocp_context>) return "iocp";
        else return "ctx";
#endif
    }

    // Unique per-case file names: parallel ctest runs of this binary next to other test
    // binaries (and reruns after a crashed run) must never collide on a shared temp dir.
    auto unique_temp_path(std::string_view case_tag, std::string_view context_tag) -> std::filesystem::path {
        static std::atomic<unsigned> counter{0};
#if COIO_OS_WINDOWS
        const auto pid = static_cast<unsigned long>(::_getpid());
#else
        const auto pid = static_cast<unsigned long>(::getpid());
#endif
        std::string name{"coio_test_file_"};
        name += case_tag;
        name += '_';
        name += context_tag;
        name += '_';
        name += std::to_string(pid);
        name += '_';
        name += std::to_string(std::random_device{}());
        name += '_';
        name += std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
        name += ".bin";
        return std::filesystem::temp_directory_path() / name;
    }

    // scope guard: the file is removed even when a failed CHECK unwinds the case early
    auto remove_on_exit(const std::filesystem::path& path) {
        return coio::scope_exit{[&path]() noexcept {
            std::error_code discard;
            std::filesystem::remove(path, discard);
        }};
    }

    [[maybe_unused]]
    auto make_payload(std::size_t size, std::uint8_t salt) -> std::vector<std::byte> {
        std::vector<std::byte> payload(size);
        for (std::size_t i = 0; i < size; ++i) {
            payload[i] = static_cast<std::byte>((i * 131 + 17 + salt) & 0xff);
        }
        return payload;
    }

#ifdef COIO_FILE_TEST_CONTEXTS
    // --- async task helpers ----------------------------------------------------

    template<typename Scheduler>
    auto stream_roundtrip_task(stream_file_t<Scheduler>& file, std::span<const std::byte> payload) -> coio::task<> {
        auto [write_ec, written] = co_await coio::async_write(file, payload);
        CHECK_FALSE(write_ec);
        CHECK_EQ(written, payload.size());

        CHECK_EQ(file.seek(0, stream_file_t<Scheduler>::seek_set), 0);

        std::vector<std::byte> readback(payload.size());
        auto [read_ec, read_n] = co_await coio::async_read(file, coio::as_writable_bytes(readback));
        CHECK_FALSE(read_ec);
        CHECK_EQ(read_n, payload.size());
        CHECK(std::ranges::equal(readback, payload));

        // the stream position now sits at end-of-file: reading 0 bytes into a non-empty
        // buffer is EOF, surfaced on the error channel as coio::error::eof
        char tail[8];
        try {
            (void) co_await file.async_read_some(coio::as_writable_bytes(tail));
            FAIL("expected coio::error::eof at end of file");
        }
        catch (const std::system_error& e) {
            CHECK_EQ(e.code(), coio::error::eof);
        }
    }

    template<typename Scheduler>
    auto composed_random_access_task(random_access_file_t<Scheduler>& file, std::span<const std::byte> payload) -> coio::task<> {
        auto [write_ec, written] = co_await coio::async_write_at(file, 0, payload);
        CHECK_FALSE(write_ec);
        CHECK_EQ(written, payload.size());

        std::vector<std::byte> readback(payload.size());
        auto [read_ec, read_n] = co_await coio::async_read_at(file, 0, coio::as_writable_bytes(readback));
        CHECK_FALSE(read_ec);
        CHECK_EQ(read_n, payload.size());
        CHECK(std::ranges::equal(readback, payload));

        std::vector<std::byte> past(64);
        auto [eof_ec, eof_n] = co_await coio::async_read_at(file, payload.size() + 512, coio::as_writable_bytes(past));
        CHECK_EQ(eof_ec, coio::error::eof);
        CHECK_EQ(eof_n, 0);
    }

    template<typename Scheduler>
    auto zero_length_async_task(stream_file_t<Scheduler>& stream, random_access_file_t<Scheduler>& random) -> coio::task<> {
        const std::size_t stream_read = co_await stream.async_read_some({});
        CHECK_EQ(stream_read, 0);
        const std::size_t stream_write = co_await stream.async_write_some({});
        CHECK_EQ(stream_write, 0);
        const std::size_t random_read = co_await random.async_read_some_at(0, {});
        CHECK_EQ(random_read, 0);
        // even far past the end an empty read completes with 0 on the value channel — the
        // eof mapping only applies to 0 bytes into a NON-empty buffer
        const std::size_t random_past_end = co_await random.async_read_some_at(std::size_t{1} << 20, {});
        CHECK_EQ(random_past_end, 0);
    }
#endif // COIO_FILE_TEST_CONTEXTS
}

#ifdef COIO_FILE_TEST_CONTEXTS

TEST_CASE_TEMPLATE("file: stream_file async write/seek/read roundtrip then eof", Context, COIO_FILE_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;
    using file_t = stream_file_t<scheduler_t>;

    const auto path = unique_temp_path("stream_async", context_tag<Context>());
    const auto guard = remove_on_exit(path);

    file_t file{scheduler, path.string(), file_t::read_write | file_t::create | file_t::truncate};
    REQUIRE(file.is_open());
    REQUIRE(bool(file));

    const auto payload = make_payload(8000, 1);
    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, stream_roundtrip_task(file, coio::as_bytes(payload))),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("file: stream_file sync write/seek/read roundtrip then eof", Context, COIO_FILE_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;
    using file_t = stream_file_t<scheduler_t>;

    const auto path = unique_temp_path("stream_sync", context_tag<Context>());
    const auto guard = remove_on_exit(path);

    file_t file{scheduler, path.string(), file_t::read_write | file_t::create | file_t::truncate};
    REQUIRE(file.is_open());

    // synchronous members never touch the completion queue, so no driver is needed
    const auto payload = make_payload(6000, 2);
    CHECK_EQ(coio::write(file, coio::as_bytes(payload)), payload.size());
    CHECK_EQ(file.seek(0, file_t::seek_set), 0);

    std::vector<std::byte> readback(payload.size());
    CHECK_EQ(coio::read(file, coio::as_writable_bytes(readback)), payload.size());
    CHECK(readback == payload);

    char tail[8];
    try {
        (void) file.read_some(coio::as_writable_bytes(tail));
        FAIL("expected coio::error::eof at end of file");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), coio::error::eof);
    }

    file.close();
    CHECK_FALSE(file.is_open());
}

TEST_CASE_TEMPLATE("file: random_access_file positional ops, eof at end, short read straddling end", Context, COIO_FILE_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;
    using file_t = random_access_file_t<scheduler_t>;

    const auto path = unique_temp_path("random_sync", context_tag<Context>());
    const auto guard = remove_on_exit(path);

    file_t file{scheduler, path.string(), file_t::read_write | file_t::create | file_t::truncate};
    REQUIRE(file.is_open());

    const auto front = make_payload(512, 3);
    const auto back = make_payload(256, 4);
    constexpr std::size_t gap_offset = 4096;

    CHECK_EQ(coio::write_at(file, 0, coio::as_bytes(front)), front.size());
    CHECK_EQ(coio::write_at(file, gap_offset, coio::as_bytes(back)), back.size());
    const std::size_t total = gap_offset + back.size();
    CHECK_EQ(file.size(), total);

    std::vector<std::byte> front_readback(front.size());
    CHECK_EQ(coio::read_at(file, 0, coio::as_writable_bytes(front_readback)), front.size());
    CHECK(front_readback == front);

    std::vector<std::byte> back_readback(back.size());
    CHECK_EQ(coio::read_at(file, gap_offset, coio::as_writable_bytes(back_readback)), back.size());
    CHECK(back_readback == back);

    // the unwritten gap between the two blocks reads back as zeros
    std::vector<std::byte> hole(64);
    CHECK_EQ(coio::read_at(file, front.size(), coio::as_writable_bytes(hole)), hole.size());
    CHECK(std::ranges::all_of(hole, [](std::byte b) { return b == std::byte{}; }));

    // reading AT the end of the data (and past it) is eof: asio parity
    char tail[8];
    try {
        (void) file.read_some_at(total, coio::as_writable_bytes(tail));
        FAIL("expected coio::error::eof reading at the end of the file");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), coio::error::eof);
    }
    try {
        (void) file.read_some_at(total + 128, coio::as_writable_bytes(tail));
        FAIL("expected coio::error::eof reading past the end of the file");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), coio::error::eof);
    }

    // a read straddling the end is a SHORT read (n == remaining), NOT eof
    constexpr std::size_t remaining = 100;
    std::vector<std::byte> straddle(256);
    const std::size_t n = file.read_some_at(total - remaining, coio::as_writable_bytes(straddle));
    CHECK_EQ(n, remaining);
    CHECK(std::ranges::equal(std::span{straddle}.first(n), std::span{back}.last(remaining)));
}

TEST_CASE_TEMPLATE("file: composed async_read_at/async_write_at complete in the value channel", Context, COIO_FILE_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;
    using file_t = random_access_file_t<scheduler_t>;

    const auto path = unique_temp_path("random_async", context_tag<Context>());
    const auto guard = remove_on_exit(path);

    file_t file{scheduler, path.string(), file_t::read_write | file_t::create | file_t::truncate};
    REQUIRE(file.is_open());

    const auto payload = make_payload(8192, 5);
    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, composed_random_access_task(file, coio::as_bytes(payload))),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("file: zero-length reads and writes complete with 0, never eof", Context, COIO_FILE_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    const auto stream_path = unique_temp_path("zero_stream", context_tag<Context>());
    const auto stream_guard = remove_on_exit(stream_path);
    const auto random_path = unique_temp_path("zero_random", context_tag<Context>());
    const auto random_guard = remove_on_exit(random_path);

    using stream_t = stream_file_t<scheduler_t>;
    using random_t = random_access_file_t<scheduler_t>;
    stream_t stream{scheduler, stream_path.string(), stream_t::read_write | stream_t::create | stream_t::truncate};
    random_t random{scheduler, random_path.string(), random_t::read_write | random_t::create | random_t::truncate};

    // sync: both files are EMPTY, yet an empty-buffer read is 0, not eof — even past end
    CHECK_EQ(stream.read_some(std::span<std::byte>{}), 0);
    CHECK_EQ(stream.write_some(std::span<const std::byte>{}), 0);
    CHECK_EQ(random.read_some_at(0, std::span<std::byte>{}), 0);
    CHECK_EQ(random.read_some_at(std::size_t{1} << 20, std::span<std::byte>{}), 0);

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, zero_length_async_task(stream, random)),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("file: resize and seek agree on the file size", Context, COIO_FILE_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;
    using file_t = stream_file_t<scheduler_t>;

    const auto path = unique_temp_path("resize_seek", context_tag<Context>());
    const auto guard = remove_on_exit(path);

    file_t file{scheduler, path.string(), file_t::read_write | file_t::create | file_t::truncate};
    REQUIRE(file.is_open());
    CHECK_EQ(file.size(), 0);

    // grow, then confirm both size() and seek(0, seek_end) observe the new size
    file.resize(12345);
    CHECK_EQ(file.size(), 12345);
    CHECK_EQ(file.seek(0, file_t::seek_end), 12345);

    // seek returns the new absolute position for every whence
    CHECK_EQ(file.seek(100, file_t::seek_set), 100);
    CHECK_EQ(file.seek(50, file_t::seek_cur), 150);

    // shrink below the current position; size and seek_end follow
    file.resize(64);
    CHECK_EQ(file.size(), 64);
    CHECK_EQ(file.seek(0, file_t::seek_end), 64);

    // random_access_file exposes the same resize/size surface (no seek)
    const auto random_path = unique_temp_path("resize_random", context_tag<Context>());
    const auto random_guard = remove_on_exit(random_path);
    using random_t = random_access_file_t<scheduler_t>;
    random_t random{scheduler, random_path.string(), random_t::read_write | random_t::create | random_t::truncate};
    random.resize(4096);
    CHECK_EQ(random.size(), 4096);
}

TEST_CASE_TEMPLATE("file: opening an already-open file reports already_open", Context, COIO_FILE_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    const auto path = unique_temp_path("already_open", context_tag<Context>());
    const auto guard = remove_on_exit(path);
    const std::string path_str = path.string();

    using stream_t = stream_file_t<scheduler_t>;
    stream_t stream{scheduler, path_str, stream_t::read_write | stream_t::create | stream_t::truncate};
    REQUIRE(stream.is_open());
    try {
        stream.open(path_str, stream_t::read_only);
        FAIL("expected coio::error::already_open");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), coio::error::already_open);
    }

    using random_t = random_access_file_t<scheduler_t>;
    random_t random{scheduler, path_str, random_t::read_write};
    REQUIRE(random.is_open());
    try {
        random.open(path_str, random_t::read_only);
        FAIL("expected coio::error::already_open");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), coio::error::already_open);
    }
}

#endif // COIO_FILE_TEST_CONTEXTS

#if COIO_OS_LINUX

TEST_CASE("file: epoll_context rejects regular files and directories") {
    std::optional<coio::epoll_context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using file_t = stream_file_t<coio::epoll_context::scheduler>;

    const auto path = unique_temp_path("epoll_reject", "epoll");
    const auto guard = remove_on_exit(path);
    {
        std::ofstream out{path};
        out << "regular file";
        REQUIRE(out.good());
    }

    file_t closed{scheduler}; // a closed wrapper on epoll is allowed
    CHECK_FALSE(closed.is_open());

    try {
        file_t file{scheduler, path.string(), file_t::read_only};
        FAIL("expected std::errc::operation_not_permitted: epoll cannot wait on regular files");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), std::errc::operation_not_permitted);
    }

    try {
        file_t dir{scheduler, std::filesystem::temp_directory_path().string(), file_t::read_only};
        FAIL("expected std::errc::operation_not_permitted: epoll cannot wait on directories");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), std::errc::operation_not_permitted);
    }
}
#endif // COIO_OS_LINUX
