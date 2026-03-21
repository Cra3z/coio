#include <coio/detail/error.h>

namespace coio::error {
    namespace {
        struct misc_category_t : std::error_category {
            auto name() const noexcept -> const char* override {
                return "coio.error.misc";
            }

            auto message(int ec) const -> std::string override {
                switch (ec) {
                case eof:
                    return "end of file";
                case already_open:
                    return "already open";
                case not_found:
                    return "not found";
                case overflow:
                    return "overflow";
                default: unreachable();
                }
            }
        };
    }

    auto misc_category() noexcept -> const std::error_category& {
        static misc_category_t instance;
        return instance;
    }
}