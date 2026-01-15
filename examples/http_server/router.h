#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include "response.h"
#include "request.h"

namespace http {
    class router {
    public:
        explicit router(std::filesystem::path static_dir);

        router(const router&) = delete;

        router& operator=(const router&) = delete;

        auto route(const request& req, response& res) -> void;
        auto set_static_dir(std::filesystem::path dir) -> void;

    private:
        auto serve_home(const request& req, response& res) -> void;
        auto serve_static(const request& req, response& res) -> bool;
        auto get_content_type(const std::string& extension) -> std::string;

        std::filesystem::path static_dir_;
        std::unordered_map<std::string, std::string> mime_types_;
        std::unordered_map<std::filesystem::path, std::vector<char>> files_;
    };
}