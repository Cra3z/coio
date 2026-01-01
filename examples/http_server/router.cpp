#include "router.h"

namespace http {
    void router::route(const request& req, response& res) {
        if (req.method == "GET") {
            res.status = response::ok;
            res.headers.emplace("Content-Type", "text/html; charset=utf-8");
            res.content = std::format(R"html(
                <!doctype html>
                <html>
                    <head><title>coio http server</title></head>
                <body>
                    <h1>Hello from coio http server</h1>
                    <p>Method: {}</p>
                    <p>Path: {}</p>
                </body>
                </html>
            )html", req.method, req.path);
            res.headers.emplace("Content-Length", std::to_string(res.content.size()));
            return;
        }

        res = response::stock_reply(response::method_not_allowed);
    }
}