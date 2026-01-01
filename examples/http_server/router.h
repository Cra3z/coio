#pragma once
#include "response.h"
#include "request.h"

namespace http {
    class router {
    public:
        router() = default;

        router(const router&) = delete;

        router& operator=(const router&) = delete;

        void route(const request& req, response& res);
    };
}