#include <string>
#include <coio/asyncio/io.h>
#include "response.h"

namespace http {
    static std::string_view status_string(response::status_type status) {
        switch (status) {
        case response::ok: return "200 OK";
        case response::created: return "201 Created";
        case response::accepted: return "202 Accepted";
        case response::no_content: return "204 No Content";
        case response::multiple_choices: return "300 Multiple Choices";
        case response::moved_permanently: return "301 Moved Permanently";
        case response::moved_temporarily: return "302 Moved Temporarily";
        case response::not_modified: return "304 Not Modified";
        case response::bad_request: return "400 Bad Request";
        case response::unauthorized: return "401 Unauthorized";
        case response::forbidden: return "403 Forbidden";
        case response::not_found: return "404 Not Found";
        case response::method_not_allowed: return "405 Method Not Allowed";
        case response::internal_server_error: return "500 Internal Server Error";
        case response::not_implemented: return "501 Not Implemented";
        case response::bad_gateway: return "502 Bad Gateway";
        case response::service_unavailable: return "503 Service Unavailable";
        default: return "500 Internal Server Error";
        }
    }

    static std::string stock_content(response::status_type status) {
        switch (status) {
        case response::ok: return "";
        case response::bad_request: return "Bad Request\n";
        case response::forbidden: return "Forbidden\n";
        case response::not_found: return "Not Found\n";
        case response::method_not_allowed: return "Method Not Allowed\n";
        case response::not_implemented: return "Not Implemented\n";
        case response::internal_server_error: return "Internal Server Error\n";
        default: return "\n";
        }
    }

    auto response::write_to(tcp_socket& socket) -> coio::task<> {
        using namespace std::string_view_literals;
        co_await coio::async_write(socket, coio::as_bytes("HTTP/1.1 "sv));
        co_await coio::async_write(socket, coio::as_bytes(status_string(status)));
        co_await coio::async_write(socket, coio::as_bytes("\r\n"sv));

        for (const auto& [name, value] : headers) {
            co_await coio::async_write(socket, coio::as_bytes(name));
            co_await coio::async_write(socket, coio::as_bytes(": "sv));
            co_await coio::async_write(socket, coio::as_bytes(value));
            co_await coio::async_write(socket, coio::as_bytes("\r\n"sv));
        }

        co_await coio::async_write(socket, coio::as_bytes("\r\n"sv));

        co_await coio::async_write(socket, coio::as_bytes(content));
    }


    auto response::stock_reply(status_type status) -> response {
        response rep;
        rep.status = status;
        rep.content = stock_content(status);
        rep.headers.emplace("Content-Length", std::to_string(rep.content.size()));
        rep.headers.emplace("Content-Type", "text/plain");
        return rep;
    }
}