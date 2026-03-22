#pragma once
#include <charconv>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <coio/core.h>
#include <coio/asyncio/io.h>
#include <coio/net/resolver.h>
#include <coio/net/tcp.h>
#include <coio/ssl/stream.h>
#include <coio/utils/flat_buffer.h>

#if COIO_OS_LINUX
#include <coio/asyncio/epoll_context.h>
namespace smtp {
    using io_context = coio::epoll_context;
}
#elif COIO_OS_WINDOWS
#include <coio/asyncio/iocp_context.h>
namespace smtp {
    using io_context = coio::iocp_context;
}
#endif

namespace smtp {
    using tcp_socket = coio::tcp::socket<io_context::scheduler>;
    using tcp_resolver = coio::tcp::resolver<io_context::scheduler>;
    using ssl_stream = coio::ssl::stream<tcp_socket>;

    enum class security_mode {
        none,
        ssl,
        starttls,
    };

    struct email_message {
        std::string from;
        std::string to;
        std::string subject;
        std::string body;
    };

    [[nodiscard]]
    inline auto base64_encode(std::string_view input) -> std::string {
        static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string output;
        int value = 0;
        int value_bits = -6;
        for (unsigned char ch : input) {
            value = (value << 8) + ch;
            value_bits += 8;
            while (value_bits >= 0) {
                output.push_back(table[(value >> value_bits) & 0x3f]);
                value_bits -= 6;
            }
        }
        if (value_bits > -6) {
            output.push_back(table[((value << 8) >> (value_bits + 8)) & 0x3f]);
        }
        while (output.size() % 4 != 0) {
            output.push_back('=');
        }
        return output;
    }

    [[nodiscard]]
    inline auto normalize_body(std::string_view body) -> std::string {
        std::string normalized;
        normalized.reserve(body.size() + 32);
        bool line_start = true;
        for (std::size_t index = 0; index < body.size(); ++index) {
            const char ch = body[index];
            if (line_start and ch == '.') {
                normalized.push_back('.');
            }
            if (ch == '\n') {
                if (index == 0 or body[index - 1] != '\r') {
                    normalized.append("\r\n");
                }
                else {
                    normalized.push_back('\n');
                }
                line_start = true;
                continue;
            }
            normalized.push_back(ch);
            line_start = (ch == '\r');
        }
        if (normalized.empty() or not normalized.ends_with("\r\n")) {
            normalized.append("\r\n");
        }
        return normalized;
    }

    [[nodiscard]]
    inline auto parse_security_mode(std::string_view value) -> security_mode {
        if (value == "none") return security_mode::none;
        if (value == "ssl") return security_mode::ssl;
        if (value == "starttls") return security_mode::starttls;
        throw std::invalid_argument{"security must be one of: none, ssl, starttls"};
    }

    [[nodiscard]]
    inline auto parse_verify_mode(std::string_view value) -> bool {
        if (value == "verify") return true;
        if (value == "noverify") return false;
        throw std::invalid_argument{"verify mode must be either: verify or noverify"};
    }

    class client {
    public:
        client(
            io_context::scheduler sched,
            std::string username,
            std::string password,
            security_mode security,
            bool verify_peer
        ) : sched_(sched),
            username_(std::move(username)),
            password_(std::move(password)),
            security_(security),
            verify_peer_(verify_peer),
            socket_(sched) {}

        auto async_connect(std::string host, std::uint16_t port) -> coio::task<> {
            host_ = std::move(host);

            tcp_resolver resolver{sched_};
            auto results = co_await resolver.async_resolve({.host_name = host_, .service_name = std::to_string(port)});

            bool connected = false;
            for (auto&& result : results) {
                co_await socket_.async_connect(result.endpoint);
                connected = true;
                break;
            }
            if (not connected) {
                throw std::system_error{coio::error::make_error_code(coio::error::not_found), "resolve"};
            }

            if (security_ == security_mode::ssl) {
                attach_tls();
                co_await tls_stream_->async_handshake(coio::ssl::handshake_type::client);
            }

            co_await async_read_response();
        }

        auto async_send(email_message message) -> coio::task<> {
            co_await async_send_command(std::format("EHLO {}\r\n", host_));

            if (security_ == security_mode::starttls) {
                co_await async_send_command("STARTTLS\r\n");
                attach_tls();
                co_await tls_stream_->async_handshake(coio::ssl::handshake_type::client);
                co_await async_send_command(std::format("EHLO {}\r\n", host_));
            }

            co_await async_send_command("AUTH LOGIN\r\n");
            co_await async_send_command(base64_encode(username_) + "\r\n");
            co_await async_send_command(base64_encode(password_) + "\r\n");
            co_await async_send_command(std::format("MAIL FROM:<{}>\r\n", message.from));
            co_await async_send_command(std::format("RCPT TO:<{}>\r\n", message.to));

            std::string payload = std::format(
                "DATA\r\n"
                "Subject: {}\r\n"
                "From: {}\r\n"
                "To: {}\r\n"
                "\r\n{}"
                ".\r\n",
                message.subject,
                message.from,
                message.to,
                normalize_body(message.body)
            );
            co_await async_send_command(payload);
            co_await async_send_command("QUIT\r\n");
        }

    private:
        auto attach_tls() -> void {
            if (tls_stream_) return;

            tls_context_.emplace(coio::ssl::method::tls_client);
            if (verify_peer_) {
                tls_context_->set_verify_mode(coio::ssl::verify_mode::peer);
                tls_context_->set_default_verify_paths();
            }

            tls_stream_.emplace(std::move(socket_), *tls_context_);
            if (verify_peer_) {
                tls_stream_->set_host_name(host_.c_str());
            }
            else {
                tls_stream_->set_verify_mode(coio::ssl::verify_mode::none);
                tls_stream_->set_server_name(host_.c_str());
            }
        }

        auto async_send_command(const std::string& command) -> coio::task<> {
            co_await async_write(command);
            co_await async_read_response();
        }

        auto async_write(std::string_view command) -> coio::task<> {
            if (tls_stream_) {
                co_await coio::async_write(*tls_stream_, coio::as_bytes(command));
            }
            else {
                co_await coio::async_write(socket_, coio::as_bytes(command));
            }
        }

        auto async_read_line() -> coio::task<std::string> {
            std::size_t consumed = 0;
            if (tls_stream_) {
                consumed = co_await coio::async_read_until(*tls_stream_, read_buffer_, "\r\n");
            }
            else {
                consumed = co_await coio::async_read_until(socket_, read_buffer_, "\r\n");
            }

            const auto data = read_buffer_.data();
            std::string line{reinterpret_cast<const char*>(data.data()), consumed - 2};
            read_buffer_.consume(consumed);
            co_return line;
        }

        auto async_read_response() -> coio::task<> {
            while (true) {
                std::string line = co_await async_read_line();
                if (line.size() < 4) {
                    throw std::runtime_error{"invalid response from SMTP server"};
                }

                int status = 0;
                const auto [_, ec] = std::from_chars(line.data(), line.data() + 3, status);
                if (ec != std::errc{}) {
                    throw std::system_error{std::make_error_code(ec), "invalid SMTP status code"};
                }
                if (line[0] != '2' and line[0] != '3') {
                    throw std::runtime_error{std::format("SMTP error {}: {}", status, line)};
                }
                if (line[3] != '-') {
                    co_return;
                }
            }
        }

        io_context::scheduler sched_;
        std::string username_;
        std::string password_;
        security_mode security_;
        bool verify_peer_;
        std::string host_;
        tcp_socket socket_;
        std::optional<coio::ssl::context> tls_context_;
        std::optional<ssl_stream> tls_stream_;
        coio::flat_buffer read_buffer_;
    };
}