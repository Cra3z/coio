#include <string>
#include <coio/asyncio/io.h>
#include "response.h"

namespace http {
    static auto status_string(response::status_type status) -> std::string_view {
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

    static auto stock_content(response::status_type status) -> std::string_view {
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
        std::string line_and_headers;
        line_and_headers.reserve(512);

        line_and_headers += std::format("HTTP/1.1 {}\r\n", status_string(status));

        for (const auto& [name, value] : headers) {
            line_and_headers += std::format("{}: {}\r\n", name, value);
        }

        line_and_headers += "\r\n";
        co_await coio::async_write(socket, coio::as_bytes(line_and_headers));

        co_await coio::async_write(socket, content);
    }


    auto response::stock_reply(status_type status) -> response {
        response rep;
        rep.status = status;
        rep.headers.emplace("Content-Length", std::to_string(rep.content.size()));
        rep.headers.emplace("Content-Type", "text/plain");
        std::string_view default_content = stock_content(status);
        rep.content = coio::as_bytes(default_content);
        return rep;
    }
}