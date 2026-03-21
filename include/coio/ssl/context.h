#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "types.h"
#include "../detail/config.h"
#include "../utils/zstring_view.h"

#if not COIO_HAS_SSL
#error "coio::ssl requires building coio with COIO_WITH_SSL=ON"
#endif

typedef struct ssl_ctx_st SSL_CTX;

namespace coio::ssl {
    class context {
    public:
        using native_handle_type = ::SSL_CTX*;
        using password_callback_type = std::function<std::string(std::size_t, bool)>;

    public:
        explicit context(native_handle_type ctx) noexcept : ctx_(ctx) {}

        explicit context(method m);

        context(const context&) = delete;

        context(context&& other) noexcept : ctx_(std::exchange(other.ctx_, nullptr)) {}

        ~context();

        auto operator= (context other) noexcept -> context& {
            std::swap(ctx_, other.ctx_);
            return *this;
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto native_handle() const noexcept -> native_handle_type {
            return ctx_;
        }

        auto set_verify_mode(verify_mode mode) -> void;

        auto set_verify_depth(int depth) -> void;

        auto set_default_verify_paths() -> void;

        auto load_verify_file(zstring_view filename) -> void;

        auto add_verify_path(zstring_view path) -> void;

        auto use_certificate_file(zstring_view filename, file_format format = file_format::pem) -> void;

        auto use_certificate_chain_file(zstring_view filename) -> void;

        auto use_private_key_file(zstring_view filename, file_format format = file_format::pem) -> void;

        auto check_private_key() -> void;

        auto use_tmp_dh_file(zstring_view filename) -> void;

        auto set_password_callback(password_callback_type callback) -> void;

        auto set_cipher_list(zstring_view ciphers) -> void;

        auto set_options(std::uint64_t options) -> std::uint64_t;

        auto clear_options(std::uint64_t options) -> std::uint64_t;

        auto set_min_protocol_version(int version) -> void;

        auto set_max_protocol_version(int version) -> void;

    private:
        static auto password_callback_impl(char* buffer, int size, int rwflag, void* userdata) -> int;

    private:
        native_handle_type ctx_;
        password_callback_type password_cb_;
    };
}
