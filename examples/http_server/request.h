#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace http {
    namespace detail {
        struct ci_less {
            using is_transparent = void;

            static auto lower(char chr) noexcept {
                return std::tolower(chr);
            }

            auto operator() (std::string_view lhs, std::string_view rhs) const noexcept -> bool {
                return std::ranges::lexicographical_compare(lhs, rhs, {}, lower, lower);
            }
        };
    }

    struct request {
        std::string method;
        std::string path;
        int http_version_major = 1;
        int http_version_minor = 1;
        std::multimap<std::string, std::string, detail::ci_less> headers;
        std::string body;
    };
}