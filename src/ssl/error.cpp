#include <openssl/err.h>
#include <coio/detail/error.h>

namespace coio::error {
    namespace {
        class ssl_category_t final : public std::error_category {
        public:
            auto name() const noexcept -> const char* override {
                return "coio::ssl";
            }

            auto message(int ec) const -> std::string override {
                char buffer[256];
                ::ERR_error_string_n(static_cast<unsigned int>(ec), buffer, std::ranges::size(buffer));
                return buffer;
            }
        };
    }

    auto ssl_category() noexcept -> const std::error_category& {
        static ssl_category_t instance;
        return instance;
    }
}
