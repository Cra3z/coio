#include <fstream>
#include <coio/asyncio/io.h>
#include "router.h"

namespace http {
    namespace {
        auto init_mime_types() -> std::unordered_map<std::string, std::string> {
            return {
                {".html", "text/html; charset=utf-8"},
                {".htm", "text/html; charset=utf-8"},
                {".css", "text/css; charset=utf-8"},
                {".js", "application/javascript; charset=utf-8"},
                {".json", "application/json; charset=utf-8"},
                {".xml", "application/xml; charset=utf-8"},
                {".txt", "text/plain; charset=utf-8"},
                {".png", "image/png"},
                {".jpg", "image/jpeg"},
                {".jpeg", "image/jpeg"},
                {".gif", "image/gif"},
                {".svg", "image/svg+xml"},
                {".ico", "image/x-icon"},
                {".webp", "image/webp"},
                {".woff", "font/woff"},
                {".woff2", "font/woff2"},
                {".ttf", "font/ttf"},
                {".otf", "font/otf"},
                {".pdf", "application/pdf"},
                {".zip", "application/zip"},
            };
        }
    }

    router::router(std::filesystem::path static_dir) : static_dir_(std::move(static_dir)), mime_types_(init_mime_types()) {
        for (const auto& entry : std::filesystem::directory_iterator(static_dir_)) {
            if (!entry.is_regular_file()) continue;
            const auto& path = entry.path();
            std::ifstream file{path, std::ios::binary};
            if (not file) throw std::runtime_error{std::format("cannot open file: {}", path.string())};
            files_.try_emplace(
                entry.path(),
                std::istreambuf_iterator{file},
                std::istreambuf_iterator<char>{}
            );
        }
    }

    auto router::route(const request& req, response& res) const -> void {
        if (req.method != "GET") {
            res = response::stock_reply(response::method_not_allowed);
            return;
        }

        // Try to serve static files first
        if (serve_static(req, res)) {
            return;
        }

        // Handle routes
        if (req.path == "/" || req.path == "/index.html") {
            serve_home(req, res);
            return;
        }

        // 404 for unknown routes
        res = response::stock_reply(response::not_found);
    }

    auto router::serve_home(const request& req, response& res) const -> void {
        std::filesystem::path index_file_path = static_dir_ / "index.html";
        res.status = response::ok;
        res.content = coio::as_bytes(files_.at(index_file_path));
        res.headers.emplace("Content-Type", "text/html; charset=utf-8");
        res.headers.emplace("Content-Length", std::to_string(res.content.size()));
    }

    auto router::serve_static(const request& req, response& res) const -> bool {
        // Check if path starts with /static/
        if (!req.path.starts_with("/static/")) {
            return false;
        }

        // Extract the file path after /static/
        std::string relative_path = req.path.substr(8); // Remove "/static/"

        // Prevent directory traversal attacks
        if (relative_path.find("..") != std::string::npos) {
            res = response::stock_reply(response::forbidden);
            return true;
        }

        std::filesystem::path file_path = static_dir_ / relative_path;

        // Check if file exists and is a regular file
        std::error_code ec;
        if (not std::filesystem::exists(file_path, ec) or !std::filesystem::is_regular_file(file_path, ec)) {
            return false; // Let other handlers deal with 404
        }

        res.status = response::ok;
        res.content = coio::as_bytes(files_.at(file_path));
        res.headers.emplace("Content-Type", get_content_type(file_path.extension().string()));
        res.headers.emplace("Content-Length", std::to_string(res.content.size()));
        res.headers.emplace("Cache-Control", "public, max-age=3600");

        return true;
    }

    auto router::get_content_type(const std::string& extension) const -> std::string {
        auto it = mime_types_.find(extension);
        if (it != mime_types_.end()) {
            return it->second;
        }
        return "application/octet-stream";
    }
}