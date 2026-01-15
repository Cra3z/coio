#include <algorithm>
#include <charconv>
#include <istream>
#include <ranges>
#include <coio/asyncio/io.h>
#include <coio/utils/streambuf.h>
#include "connection.h"
#include "request.h"
#include "response.h"
#include "../common.h"

namespace http {
    namespace {
        auto should_keep_alive(const request& req) noexcept -> bool {
            if (auto it = req.headers.find("Connection"); it != req.headers.end()) {
                return it->second == "keep-alive";
            }
            return req.http_version_major > 1 or (req.http_version_major == 1 && req.http_version_minor >= 1);
        }

        auto trim(std::string_view str) -> std::string {
            while (not str.empty() and std::isspace(str.front())) {
                str.remove_prefix(1);
            }
            while (not str.empty() and std::isspace(str.back())) {
                str.remove_suffix(1);
            }
            return std::string(str);
        }

        auto parse_content_length(const request& req) -> std::optional<std::size_t> {
            auto it = req.headers.find("Content-Length");
            if (it == req.headers.end()) return {};
            auto v = trim(it->second);
            if (v.empty()) return {};
            std::size_t content_length = 0;
            auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), content_length);
            if (ec != std::errc{} or p != v.data() + v.size()) return {};
            return content_length;
        }

        auto getline_crlf(std::istream& is) -> std::string {
            std::string line;
            std::getline(is, line);
            if (not line.empty() and line.back() == '\r') line.pop_back();
            return line;
        }

        auto parse_http_version(std::string_view ver) -> std::optional<std::pair<int, int>> {
            if (!ver.starts_with("HTTP/")) return std::nullopt;
            ver.remove_prefix(5);
            auto dot = ver.find('.');
            if (dot == std::string_view::npos) return std::nullopt;

            auto parse_int = [](std::string_view s) -> std::optional<int> {
                int v = 0;
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
                return (ec == std::errc{} and p == s.data() + s.size()) ? std::optional{v} : std::nullopt;
            };

            auto major = parse_int(ver.substr(0, dot));
            auto minor = parse_int(ver.substr(dot + 1));
            if (!major or !minor) return std::nullopt;
            return std::pair{*major, *minor};
        }

        auto parse_line_and_headers(std::istream& is) -> std::optional<request> {
            request req;

            // Request line: METHOD SP URI SP HTTP/x.y
            auto request_line = getline_crlf(is);
            if (request_line.empty() or !is) return std::nullopt;

            auto sp1 = request_line.find(' ');
            auto sp2 = (sp1 != std::string::npos) ? request_line.find(' ', sp1 + 1) : std::string::npos;
            if (sp2 == std::string::npos) return std::nullopt;

            req.method = request_line.substr(0, sp1);
            req.path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

            auto version = parse_http_version(request_line.substr(sp2 + 1));
            if (!version) return std::nullopt;
            req.http_version_major = version->first;
            req.http_version_minor = version->second;

            // Headers
            for (std::string line; std::getline(is, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) break;

                if (auto colon = line.find(':'); colon != std::string::npos) {
                    req.headers.emplace(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
                }
            }
            return req;
        }
    }

    auto connection(
        tcp_socket socket,
        coio::endpoint remote_endpoint,
        router& router,
        coio::inplace_stop_token stop_token
    ) -> coio::task<> try {
        while (not stop_token.stop_requested()) {
            coio::streambuf buf;
            co_await coio::async_read_until(socket, buf, "\r\n\r\n", stop_token);
            std::istream stream(&buf);
            auto maybe_req = parse_line_and_headers(stream);
            if (!maybe_req) {
                response res = response::stock_reply(response::bad_request);
                res.headers.emplace("Connection", "close");
                co_await res.write_to(socket, stop_token);
                socket.shutdown(tcp_socket::shutdown_send);
                co_return;
            }

            request req = std::move(*maybe_req);

            if (auto maybe_content_length = parse_content_length(req); maybe_content_length > 0) {
                const std::size_t content_length = *maybe_content_length;
                if (buf.size() < content_length) {
                    co_await coio::async_read(socket, buf, content_length - buf.size(), stop_token);
                }
                req.body.resize(content_length);
                stream.read(req.body.data(), static_cast<std::streamsize>(content_length));
            }

            response rep;
            router.route(req, rep);

            const bool keep_alive = should_keep_alive(req);
            rep.headers.emplace("Connection", keep_alive ? "keep-alive" : "close");
            co_await rep.write_to(socket, stop_token);
            if (not keep_alive) {
                socket.shutdown(tcp_socket::shutdown_send);
                co_return;
            }
        }
    }
    catch (const std::exception& e) {
        ::debug("connection with {} broken: {}", remote_endpoint, e.what());
    }
}