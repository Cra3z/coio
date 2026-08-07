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
#include <coio/detail/config.h>
#include <coio/detail/error.h>
#include <coio/net/basic.h>
#include <coio/net/socket.h>
#include <coio/net/tcp.h>
#include <coio/net/udp.h>

#if COIO_OS_LINUX
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
    using tcp_socket_t = coio::tcp::socket<Scheduler>;
    template<typename Scheduler>
    using tcp_acceptor_t = coio::tcp::acceptor<Scheduler>;
    template<typename Scheduler>
    using udp_socket_t = coio::udp::socket<Scheduler>;

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

    template<typename Scheduler>
    auto ipv6_loopback_available(Scheduler scheduler) -> bool {
        try {
            tcp_socket_t<Scheduler> probe{scheduler, coio::tcp::v6()};
            probe.bind(coio::endpoint{coio::ipv6_address::loopback(), 0});
            return true;
        }
        catch (const std::system_error&) {
            return false;
        }
    }

    auto as_string(std::span<const char> chars) -> std::string_view {
        return {chars.data(), chars.size()};
    }

    // --- TCP echo helpers ----------------------------------------------------

    template<typename Scheduler>
    auto echo_server(tcp_acceptor_t<Scheduler>& acceptor, std::string_view expect, std::string_view reply) -> coio::task<> {
        auto peer = co_await acceptor.async_accept();
        std::vector<char> buffer(expect.size());
        auto [read_ec, read_n] = co_await coio::async_read(peer, coio::as_writable_bytes(buffer));
        CHECK_FALSE(read_ec);
        CHECK_EQ(read_n, expect.size());
        CHECK_EQ(as_string(buffer), expect);
        auto [write_ec, write_n] = co_await coio::async_write(peer, coio::as_bytes(reply));
        CHECK_FALSE(write_ec);
        CHECK_EQ(write_n, reply.size());
    }

    template<typename Scheduler>
    auto echo_client(Scheduler scheduler, coio::tcp protocol, coio::endpoint server_endpoint,
                     std::string_view send, std::string_view expect) -> coio::task<> {
        tcp_socket_t<Scheduler> socket{scheduler, protocol};
        co_await socket.async_connect(server_endpoint);
        CHECK_EQ(socket.remote_endpoint(), server_endpoint);
        auto [write_ec, write_n] = co_await coio::async_write(socket, coio::as_bytes(send));
        CHECK_FALSE(write_ec);
        CHECK_EQ(write_n, send.size());
        std::vector<char> buffer(expect.size());
        auto [read_ec, read_n] = co_await coio::async_read(socket, coio::as_writable_bytes(buffer));
        CHECK_FALSE(read_ec);
        CHECK_EQ(read_n, expect.size());
        CHECK_EQ(as_string(buffer), expect);
    }

    // --- large-transfer helpers ----------------------------------------------

    template<typename Scheduler>
    auto sink_server(tcp_acceptor_t<Scheduler>& acceptor, std::span<std::byte> dest) -> coio::task<> {
        auto peer = co_await acceptor.async_accept();
        auto [ec, n] = co_await coio::async_read(peer, dest);
        CHECK_FALSE(ec);
        CHECK_EQ(n, dest.size());
    }

    template<typename Scheduler>
    auto blast_client(Scheduler scheduler, coio::endpoint server_endpoint, std::span<const std::byte> src) -> coio::task<> {
        tcp_socket_t<Scheduler> socket{scheduler};
        co_await socket.async_connect(server_endpoint);
        auto [ec, n] = co_await coio::async_write(socket, src);
        CHECK_FALSE(ec);
        CHECK_EQ(n, src.size());
    }

    // --- shutdown/EOF helpers ------------------------------------------------

    template<typename Scheduler>
    auto eof_server(tcp_acceptor_t<Scheduler>& acceptor) -> coio::task<> {
        auto peer = co_await acceptor.async_accept();
        char buffer[16];
        try {
            (void) co_await peer.async_read_some(coio::as_writable_bytes(buffer));
            FAIL("expected coio::error::eof after the client shut down its send side");
        }
        catch (const std::system_error& e) {
            CHECK_EQ(e.code(), coio::error::eof);
        }
    } // ~peer closes the server side, delivering EOF to the client below

    template<typename Scheduler>
    auto shutdown_client(Scheduler scheduler, coio::endpoint server_endpoint) -> coio::task<> {
        tcp_socket_t<Scheduler> socket{scheduler};
        co_await socket.async_connect(server_endpoint);
        socket.shutdown(tcp_socket_t<Scheduler>::shutdown_send);
        char buffer[16];
        try {
            (void) co_await socket.async_read_some(coio::as_writable_bytes(buffer));
            FAIL("expected coio::error::eof after the server closed the connection");
        }
        catch (const std::system_error& e) {
            CHECK_EQ(e.code(), coio::error::eof);
        }
    }

    // --- UDP helpers ---------------------------------------------------------

    template<typename Scheduler>
    auto udp_responder(udp_socket_t<Scheduler>& socket, coio::endpoint expected_peer,
                       std::string_view expect, std::string_view reply) -> coio::task<> {
        std::vector<char> buffer(expect.size() + 16);
        auto [from, n] = co_await socket.async_receive_from(coio::as_writable_bytes(buffer));
        CHECK_EQ(n, expect.size());
        CHECK_EQ(as_string({buffer.data(), n}), expect);
        CHECK_EQ(from, expected_peer);
        const std::size_t sent = co_await socket.async_send_to(coio::as_bytes(reply), from);
        CHECK_EQ(sent, reply.size());
    }

    template<typename Scheduler>
    auto udp_requester(udp_socket_t<Scheduler>& socket, coio::endpoint peer, coio::endpoint expected_from,
                       std::string_view send, std::string_view expect) -> coio::task<> {
        const std::size_t sent = co_await socket.async_send_to(coio::as_bytes(send), peer);
        CHECK_EQ(sent, send.size());
        std::vector<char> buffer(expect.size() + 16);
        auto [from, n] = co_await socket.async_receive_from(coio::as_writable_bytes(buffer));
        CHECK_EQ(n, expect.size());
        CHECK_EQ(as_string({buffer.data(), n}), expect);
        CHECK_EQ(from, expected_from);
    }

    template<typename Scheduler>
    auto udp_connected_receiver(udp_socket_t<Scheduler>& socket, std::string_view expect) -> coio::task<> {
        std::vector<char> buffer(expect.size() + 16);
        const std::size_t n = co_await socket.async_receive(coio::as_writable_bytes(buffer));
        CHECK_EQ(n, expect.size());
        CHECK_EQ(as_string({buffer.data(), n}), expect);
    }

    template<typename Scheduler>
    auto udp_connected_sender(udp_socket_t<Scheduler>& socket, std::string_view payload) -> coio::task<> {
        const std::size_t n = co_await socket.async_send(coio::as_bytes(payload));
        CHECK_EQ(n, payload.size());
    }

    // --- zero-length helpers -------------------------------------------------

    // asio parity: zero-length operations on datagram sockets are REAL — an empty send
    // transmits an empty datagram and a zero-length receive consumes one. The receivers
    // race a generous watchdog so a regression (the empty send being short-circuited
    // instead of transmitted) fails visibly instead of hanging the case.
    template<typename Scheduler>
    auto empty_datagram_receiver(udp_socket_t<Scheduler>& socket, coio::endpoint expected_peer, Scheduler scheduler) -> coio::task<> {
        std::vector<char> buffer(16);
        const int winner = co_await coio::when_any(
            socket.async_receive_from(coio::as_writable_bytes(buffer)) | coio::then([expected_peer](coio::endpoint from, std::size_t n) {
                CHECK_EQ(n, 0);
                CHECK_EQ(from, expected_peer);
                return 1;
            }),
            scheduler.schedule_after(5s) | coio::then([] { return 2; })
        );
        CHECK_EQ(winner, 1); // the empty datagram was really transmitted
    }

    template<typename Scheduler>
    auto empty_datagram_sender(udp_socket_t<Scheduler>& socket, coio::endpoint dest) -> coio::task<> {
        const std::size_t sent = co_await socket.async_send_to({}, dest);
        CHECK_EQ(sent, 0);
    }

    template<typename Scheduler>
    auto empty_connected_receiver(udp_socket_t<Scheduler>& socket, Scheduler scheduler) -> coio::task<> {
        std::vector<char> buffer(16);
        const int winner = co_await coio::when_any(
            socket.async_receive(coio::as_writable_bytes(buffer)) | coio::then([](std::size_t n) {
                CHECK_EQ(n, 0);
                return 1;
            }),
            scheduler.schedule_after(5s) | coio::then([] { return 2; })
        );
        CHECK_EQ(winner, 1); // the empty datagram was really transmitted
    }

    template<typename Scheduler>
    auto empty_connected_sender(udp_socket_t<Scheduler>& socket) -> coio::task<> {
        const std::size_t sent = co_await socket.async_send({});
        CHECK_EQ(sent, 0);
    }

    // stream no-op: with no data in flight an empty read must complete immediately with
    // 0 (never eof, never waiting for data); raced against a generous watchdog so a
    // regression fails visibly instead of hanging the case.
    template<typename Scheduler>
    auto empty_stream_server(tcp_acceptor_t<Scheduler>& acceptor, Scheduler scheduler) -> coio::task<> {
        auto peer = co_await acceptor.async_accept();
        const int winner = co_await coio::when_any(
            peer.async_read_some({}) | coio::then([](std::size_t n) {
                CHECK_EQ(n, 0);
                return 1;
            }),
            scheduler.schedule_after(5s) | coio::then([] { return 2; })
        );
        CHECK_EQ(winner, 1); // the empty read completed immediately, with 0
        const std::size_t written = co_await peer.async_write_some({});
        CHECK_EQ(written, 0);
        // sync forms: nothing is in flight, yet neither call may block or report eof
        CHECK_EQ(peer.read_some({}), 0);
        CHECK_EQ(peer.receive({}), 0);
        // unblock the client, which is waiting for one byte
        auto [ec, n] = co_await coio::async_write(peer, coio::as_bytes(std::string_view{"x"}));
        CHECK_FALSE(ec);
        CHECK_EQ(n, 1);
    }

    // --- cancellation helpers ------------------------------------------------

    // Races a pending read on a silent connection against a short timer; the timer must win
    // and when_any must cancel the read (when_any only completes after all children did).
    template<typename Scheduler>
    auto cancel_read_server(tcp_acceptor_t<Scheduler>& acceptor, Scheduler scheduler) -> coio::task<> {
        auto peer = co_await acceptor.async_accept();
        char buffer[16];
        const int winner = co_await coio::when_any(
            peer.async_read_some(coio::as_writable_bytes(buffer)) | coio::then([](std::size_t) { return 1; }),
            scheduler.schedule_after(100ms) | coio::then([] { return 2; })
        );
        CHECK_EQ(winner, 2); // the timer won: the pending read was stopped
        // unblock the client, which is waiting for one byte
        auto [ec, n] = co_await coio::async_write(peer, coio::as_bytes(std::string_view{"x"}));
        CHECK_FALSE(ec);
        CHECK_EQ(n, 1);
    }

    // Connects and then stays silent until the server tells us it is done (one byte).
    template<typename Scheduler>
    auto silent_client(Scheduler scheduler, coio::endpoint server_endpoint) -> coio::task<> {
        tcp_socket_t<Scheduler> socket{scheduler};
        co_await socket.async_connect(server_endpoint);
        char buffer[4];
        const std::size_t n = co_await socket.async_read_some(coio::as_writable_bytes(buffer));
        CHECK_EQ(n, 1);
    }

    // Races a pending accept (no client will ever connect) against a short timer.
    template<typename Scheduler>
    auto cancel_accept_task(Scheduler scheduler) -> coio::task<> {
        tcp_acceptor_t<Scheduler> acceptor{scheduler, coio::endpoint{coio::ipv4_address::loopback(), 0}};
        const int winner = co_await coio::when_any(
            acceptor.async_accept() | coio::then([](tcp_socket_t<Scheduler>) { return 1; }),
            scheduler.schedule_after(100ms) | coio::then([] { return 2; })
        );
        CHECK_EQ(winner, 2); // the timer won: the pending accept was stopped
    }

    // --- connect-refused helper ----------------------------------------------

    template<typename Scheduler>
    auto refused_client(Scheduler scheduler, coio::endpoint dead_endpoint) -> coio::task<> {
        tcp_socket_t<Scheduler> socket{scheduler};
        try {
            co_await socket.async_connect(dead_endpoint);
            FAIL("connect to a closed loopback port unexpectedly succeeded");
        }
        catch (const std::system_error& e) {
            CHECK_NE(e.code(), coio::error::eof);
            // iocp_context translates ERROR_CONNECTION_REFUSED (1225) to WSAECONNREFUSED
            // (10061), so the portable errc comparison holds on every platform.
            CHECK_EQ(e.code(), std::errc::connection_refused);
        }
    }
}

TEST_CASE_TEMPLATE("socket: tcp echo roundtrip", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    tcp_acceptor_t<scheduler_t> acceptor{scheduler, coio::endpoint{coio::ipv4_address::loopback(), 0}};
    const coio::endpoint server_endpoint = acceptor.local_endpoint();
    CHECK_NE(server_endpoint.port(), 0);
    CHECK_EQ(server_endpoint.ip(), coio::ip_address{coio::ipv4_address::loopback()});

    static constexpr std::string_view request = "ping from client";
    static constexpr std::string_view reply = "pong from server";
    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, echo_server(acceptor, request, reply)),
        coio::starts_on(scheduler, echo_client(scheduler, coio::tcp::v4(), server_endpoint, request, reply)),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("socket: tcp large transfer loops over short reads/writes", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    tcp_acceptor_t<scheduler_t> acceptor{scheduler, coio::endpoint{coio::ipv4_address::loopback(), 0}};
    const coio::endpoint server_endpoint = acceptor.local_endpoint();

    constexpr std::size_t total = std::size_t{1} << 20; // 1 MiB
    std::vector<std::byte> payload(total);
    for (std::size_t i = 0; i < total; ++i) {
        payload[i] = static_cast<std::byte>((i * 31 + 7) & 0xff);
    }
    std::vector<std::byte> received(total);

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, sink_server(acceptor, received)),
        coio::starts_on(scheduler, blast_client(scheduler, server_endpoint, payload)),
        drive(*context)
    ));

    CHECK(received == payload);
}

TEST_CASE_TEMPLATE("socket: orderly shutdown surfaces eof", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    tcp_acceptor_t<scheduler_t> acceptor{scheduler, coio::endpoint{coio::ipv4_address::loopback(), 0}};
    const coio::endpoint server_endpoint = acceptor.local_endpoint();

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, eof_server(acceptor)),
        coio::starts_on(scheduler, shutdown_client(scheduler, server_endpoint)),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("socket: udp datagram exchange reports the sender endpoint", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    // unconnected pair: send_to / receive_from both ways
    udp_socket_t<scheduler_t> alice{scheduler, coio::udp::v4()};
    alice.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});
    udp_socket_t<scheduler_t> bob{scheduler, coio::udp::v4()};
    bob.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});
    const coio::endpoint alice_endpoint = alice.local_endpoint();
    const coio::endpoint bob_endpoint = bob.local_endpoint();
    CHECK_NE(alice_endpoint, bob_endpoint);

    // connected pair: async_send / async_receive
    udp_socket_t<scheduler_t> carol{scheduler, coio::udp::v4()};
    carol.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});
    udp_socket_t<scheduler_t> dave{scheduler, coio::udp::v4()};
    dave.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});
    carol.connect(dave.local_endpoint());
    dave.connect(carol.local_endpoint());

    static constexpr std::string_view ping = "udp ping";
    static constexpr std::string_view pong = "udp pong!";
    static constexpr std::string_view connected_payload = "connected udp payload";
    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, udp_responder(bob, alice_endpoint, ping, pong)),
        coio::starts_on(scheduler, udp_requester(alice, bob_endpoint, bob_endpoint, ping, pong)),
        coio::starts_on(scheduler, udp_connected_receiver(dave, connected_payload)),
        coio::starts_on(scheduler, udp_connected_sender(carol, connected_payload)),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("socket: zero-length ops are no-ops on streams but real on datagram sockets", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    // unconnected pair: an empty async_send_to must transmit a real empty datagram
    udp_socket_t<scheduler_t> alice{scheduler, coio::udp::v4()};
    alice.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});
    udp_socket_t<scheduler_t> bob{scheduler, coio::udp::v4()};
    bob.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});

    // connected pair: an empty async_send must arrive at the peer's async_receive
    udp_socket_t<scheduler_t> carol{scheduler, coio::udp::v4()};
    carol.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});
    udp_socket_t<scheduler_t> dave{scheduler, coio::udp::v4()};
    dave.bind(coio::endpoint{coio::ipv4_address::loopback(), 0});
    carol.connect(dave.local_endpoint());
    dave.connect(carol.local_endpoint());

    // stream side: empty reads/writes on a connected tcp pair with no data in flight
    tcp_acceptor_t<scheduler_t> acceptor{scheduler, coio::endpoint{coio::ipv4_address::loopback(), 0}};

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, empty_datagram_receiver(bob, alice.local_endpoint(), scheduler)),
        coio::starts_on(scheduler, empty_datagram_sender(alice, bob.local_endpoint())),
        coio::starts_on(scheduler, empty_connected_receiver(dave, scheduler)),
        coio::starts_on(scheduler, empty_connected_sender(carol)),
        coio::starts_on(scheduler, empty_stream_server(acceptor, scheduler)),
        coio::starts_on(scheduler, silent_client(scheduler, acceptor.local_endpoint())),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("socket: pending read and accept are cancelled by a timer race", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    tcp_acceptor_t<scheduler_t> acceptor{scheduler, coio::endpoint{coio::ipv4_address::loopback(), 0}};
    const coio::endpoint server_endpoint = acceptor.local_endpoint();

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, cancel_read_server(acceptor, scheduler)),
        coio::starts_on(scheduler, silent_client(scheduler, server_endpoint)),
        coio::starts_on(scheduler, cancel_accept_task(scheduler)),
        drive(*context)
    ));
} // context must destruct cleanly here: all raced operations completed

TEST_CASE_TEMPLATE("socket: connecting to a closed loopback port is refused", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    coio::endpoint dead_endpoint{coio::ipv4_address::loopback(), 0};
    {
        // grab an ephemeral port, then close the acceptor so nothing listens on it
        tcp_acceptor_t<scheduler_t> ephemeral{scheduler, coio::endpoint{coio::ipv4_address::loopback(), 0}};
        dead_endpoint.port() = ephemeral.local_endpoint().port();
    }

    coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, refused_client(scheduler, dead_endpoint)),
        drive(*context)
    ));
}

TEST_CASE_TEMPLATE("socket: opening an already-open socket reports already_open", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    tcp_socket_t<scheduler_t> tcp_socket{scheduler, coio::tcp::v4()};
    REQUIRE(tcp_socket.is_open());
    try {
        tcp_socket.open(coio::tcp::v4());
        FAIL("expected coio::error::already_open");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), coio::error::already_open);
    }

    udp_socket_t<scheduler_t> udp_socket{scheduler, coio::udp::v4()};
    REQUIRE(udp_socket.is_open());
    try {
        udp_socket.open(coio::udp::v4());
        FAIL("expected coio::error::already_open");
    }
    catch (const std::system_error& e) {
        CHECK_EQ(e.code(), coio::error::already_open);
    }
}

TEST_CASE_TEMPLATE("socket: ipv6 echo and dual-stack v4-mapped accept", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();
    using scheduler_t = typename Context::scheduler;

    if (not ipv6_loopback_available(scheduler)) {
        MESSAGE("skipping: IPv6 loopback is not available on this host");
        return;
    }

    static constexpr std::string_view request = "ping over ipv6";
    static constexpr std::string_view reply = "pong over ipv6";

    // pure v6 echo over [::1]
    {
        tcp_acceptor_t<scheduler_t> acceptor{scheduler, coio::endpoint{coio::ipv6_address::loopback(), 0}};
        const coio::endpoint server_endpoint = acceptor.local_endpoint();
        CHECK(server_endpoint.ip().is_v6());
        coio::this_thread::sync_wait(coio::when_all(
            coio::starts_on(scheduler, echo_server(acceptor, request, reply)),
            coio::starts_on(scheduler, echo_client(scheduler, coio::tcp::v6(), server_endpoint, request, reply)),
            drive(*context)
        ));
    }

    // dual-stack: v6 acceptor with v6_only disabled accepts a v4 client (v4-mapped path)
    {
        tcp_acceptor_t<scheduler_t> acceptor{scheduler};
        acceptor.open(coio::tcp::v6());
        try {
            acceptor.set_option(typename tcp_acceptor_t<scheduler_t>::v6_only{false});
        }
        catch (const std::system_error& e) {
            MESSAGE("skipping dual-stack part: cannot disable v6_only: " << e.what());
            return;
        }
        acceptor.bind(coio::endpoint{coio::ipv6_address::any(), 0});
        acceptor.listen();
        const coio::endpoint v4_endpoint{coio::ipv4_address::loopback(), acceptor.local_endpoint().port()};
        coio::this_thread::sync_wait(coio::when_all(
            coio::starts_on(scheduler, echo_server(acceptor, request, reply)),
            coio::starts_on(scheduler, echo_client(scheduler, coio::tcp::v4(), v4_endpoint, request, reply)),
            drive(*context)
        ));
    }
}

TEST_CASE_TEMPLATE("socket: numeric resolve is deterministic", Context, COIO_TEST_CONTEXTS) {
    using resolver_t = coio::tcp::resolver<typename Context::scheduler>;
    auto results = resolver_t::resolve(coio::resolve_query_t{
        .host_name = "127.0.0.1",
        .service_name = "34567",
        .flags = coio::resolve_query_t::numeric_host | coio::resolve_query_t::numeric_service
    });
    std::size_t count = 0;
    for (auto&& entry : results) {
        CHECK_EQ(entry.endpoint, coio::endpoint{coio::ipv4_address::loopback(), 34567});
        ++count;
    }
    CHECK_GE(count, 1);
}

namespace {
    template<typename Scheduler>
    auto async_resolve_numeric(Scheduler scheduler) -> coio::task<std::size_t> {
        coio::tcp::resolver<Scheduler> resolver{scheduler};
        auto results = co_await resolver.async_resolve(coio::resolve_query_t{
            .host_name = "127.0.0.1",
            .service_name = "34567",
            .flags = coio::resolve_query_t::numeric_host | coio::resolve_query_t::numeric_service
        });
        std::size_t count = 0;
        for (auto&& entry : results) {
            CHECK_EQ(entry.endpoint, coio::endpoint{coio::ipv4_address::loopback(), 34567});
            ++count;
        }
        co_return count;
    }

    // numeric_host never consults DNS, so a non-numeric host fails deterministically; the
    // getaddrinfo error must arrive through the sender's error channel as a gai_category error.
    template<typename Scheduler>
    auto async_resolve_failure(Scheduler scheduler) -> coio::task<bool> {
        coio::tcp::resolver<Scheduler> resolver{scheduler};
        try {
            void(co_await resolver.async_resolve(coio::resolve_query_t{
                .host_name = "definitely not numeric",
                .flags = coio::resolve_query_t::numeric_host
            }));
        }
        catch (const std::system_error& e) {
            co_return e.code().category() == coio::error::gai_category();
        }
        co_return false;
    }
}

TEST_CASE_TEMPLATE("socket: async_resolve performs the lookup on the scheduler", Context, COIO_TEST_CONTEXTS) {
    std::optional<Context> context;
    if (not try_make_context(context)) return;
    auto scheduler = context->get_scheduler();

    auto [count, gai_failure] = coio::this_thread::sync_wait(coio::when_all(
        coio::starts_on(scheduler, async_resolve_numeric(scheduler)),
        coio::starts_on(scheduler, async_resolve_failure(scheduler)),
        drive(*context)
    )).value();
    CHECK_GE(count, 1);
    CHECK(gai_failure);
}
