#pragma once
#include <string>
#include "request.h"
#include "define.h"

namespace http {
    struct response {
        enum status_type {
            ok = 200,
            created = 201,
            accepted = 202,
            no_content = 204,
            multiple_choices = 300,
            moved_permanently = 301,
            moved_temporarily = 302,
            not_modified = 304,
            bad_request = 400,
            unauthorized = 401,
            forbidden = 403,
            not_found = 404,
            method_not_allowed = 405,
            internal_server_error = 500,
            not_implemented = 501,
            bad_gateway = 502,
            service_unavailable = 503
        };

        status_type status = ok;
        std::multimap<std::string, std::string, detail::ci_less> headers;
        std::span<const std::byte> content;

        auto write_to(tcp_socket& socket) -> coio::task<>;

        static auto stock_reply(status_type status) -> response;
    };
}