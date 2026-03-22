#include "../common.h"
#include "client.h"

auto start_client(smtp::client& client, std::string host, std::uint16_t port, smtp::email_message message) -> coio::task<> {
    co_await client.async_connect(std::move(host), port);
    co_await client.async_send(std::move(message));
}

auto main(int argc, char** argv) -> int try {
    if (argc != 11) {
        println(std::cerr, "usage: {} <host> <port> <security:none|ssl|starttls> <verify|noverify> <username> <password> <from> <to> <subject> <body>", argv[0]);
        println(std::cerr, R"(for example: {} "smtp.qq.com" 587 starttls noverify Alice xxxxxx Alice@someemail.com Bob@someemail.com "Hello" "Hello Bob, I'm Alice!")", argv[0]);
        return EXIT_FAILURE;
    }
    const std::string host = argv[1];
    const int port = std::stoi(argv[2]);
    const smtp::security_mode security = smtp::parse_security_mode(argv[3]);
    const bool verify_peer = smtp::parse_verify_mode(argv[4]);
    const std::string username = argv[5];
    const std::string password = argv[6];
    const smtp::email_message message {
        .from = argv[7],
        .to = argv[8],
        .subject = argv[9],
        .body = argv[10]
    };

    smtp::io_context context;
    coio::async_scope scope;
    smtp::client client{
        context.get_scheduler(),
        std::move(username),
        std::move(password),
        security,
        verify_peer
    };

    scope.spawn(start_client(client, std::move(host), port, std::move(message)));

    context.run();
    coio::this_thread::sync_wait(scope.join());
    println("email sent successfully");
}
catch (const std::exception& e) {
    println(std::cerr, "error: {}", e.what());
    return EXIT_FAILURE;
}