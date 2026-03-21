#include <cstring>
#include <openssl/dh.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <coio/detail/error.h>
#include <coio/ssl/context.h>

namespace coio::error {
    namespace {
        class ssl_category_t final : public std::error_category {
        public:
            auto name() const noexcept -> const char* override {
                return "coio.error.ssl";
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

namespace coio::ssl {
    namespace {
        struct openssl_init_guard {
            openssl_init_guard() {
                ::OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
            }
        };

        [[noreturn]]
        COIO_ALWAYS_INLINE auto throw_last_error(const char* msg) {
            throw std::system_error{static_cast<int>(::ERR_get_error()), error::ssl_category(), msg};
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto to_ssl_method(method m) -> const ::SSL_METHOD* {
            switch (m) {
            using enum method;
            case sslv23:
                return SSLv23_method();
            case sslv23_client:
                return SSLv23_client_method();
            case sslv23_server:
                return SSLv23_server_method();
            case tls:
                return ::TLS_method();
            case tls_client:
                return ::TLS_client_method();
            case tls_server:
                return ::TLS_server_method();
            default:
                throw std::system_error{std::make_error_code(std::errc::not_supported), "unsupported SSL method"};
            }
        }
    }

    context::context(method m) {
        static openssl_init_guard _{};
        ::ERR_clear_error();
        ctx_ = ::SSL_CTX_new(to_ssl_method(m));
        if (ctx_ == nullptr) [[unlikely]] {
            throw_last_error("SSL_CTX_new");
        }
    }

    context::~context() {
        ::SSL_CTX_free(ctx_);
    }

    auto context::set_verify_mode(verify_mode mode) -> void {
        ::SSL_CTX_set_verify(ctx_, to_underlying(mode), nullptr);
    }

    auto context::set_verify_depth(int depth) -> void {
        ::SSL_CTX_set_verify_depth(ctx_, depth);
    }

    auto context::set_default_verify_paths() -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_set_default_verify_paths(ctx_) != 1) {
            throw_last_error("SSL_CTX_set_default_verify_paths");
        }
    }

    auto context::load_verify_file(zstring_view filename) -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_load_verify_locations(ctx_, filename.c_str(), nullptr) != 1) {
            throw_last_error("SSL_CTX_load_verify_locations");
        }
    }

    auto context::add_verify_path(zstring_view path) -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_load_verify_locations(ctx_, nullptr, path.c_str()) != 1) {
            throw_last_error("SSL_CTX_load_verify_locations");
        }
    }

    auto context::use_certificate_file(zstring_view filename, file_format format) -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_use_certificate_file(ctx_, filename.c_str(), to_underlying(format)) != 1) {
            throw_last_error("SSL_CTX_use_certificate_file");
        }
    }

    auto context::use_certificate_chain_file(zstring_view filename) -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_use_certificate_chain_file(ctx_, filename.c_str()) != 1) {
            throw_last_error("SSL_CTX_use_certificate_chain_file");
        }
    }

    auto context::use_private_key_file(zstring_view filename, file_format format) -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_use_PrivateKey_file(ctx_, filename.c_str(), to_underlying(format)) != 1) {
            throw_last_error("SSL_CTX_use_PrivateKey_file");
        }
    }

    auto context::check_private_key() -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_check_private_key(ctx_) != 1) {
            throw_last_error("SSL_CTX_check_private_key");
        }
    }

    auto context::use_tmp_dh_file(zstring_view filename) -> void {
        ::ERR_clear_error();
        auto* bio = ::BIO_new_file(filename.c_str(), "r");
        if (bio == nullptr) {
            throw_last_error("BIO_new_file");
        }
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        auto* params = ::PEM_read_bio_Parameters(bio, nullptr);
        ::BIO_free(bio);
        if (params == nullptr) {
            throw_last_error("PEM_read_bio_Parameters");
        }
        if (::SSL_CTX_set0_tmp_dh_pkey(ctx_, params) != 1) {
            ::EVP_PKEY_free(params);
            throw_last_error("SSL_CTX_set0_tmp_dh_pkey");
        }
#else
        auto* dh = ::PEM_read_bio_DHparams(bio, nullptr, nullptr, nullptr);
        ::BIO_free(bio);
        if (dh == nullptr) {
            throw_last_error("PEM_read_bio_DHparams");
        }
        if (::SSL_CTX_set_tmp_dh(ctx_, dh) != 1) {
            ::DH_free(dh);
            throw_last_error("SSL_CTX_set_tmp_dh");
        }
        ::DH_free(dh);
#endif
    }

    auto context::set_password_callback(password_callback_type callback) -> void {
        password_cb_ = std::move(callback);
        ::SSL_CTX_set_default_passwd_cb(ctx_, &context::password_callback_impl);
        ::SSL_CTX_set_default_passwd_cb_userdata(ctx_, this);
    }

    auto context::set_cipher_list(zstring_view ciphers) -> void {
        ::ERR_clear_error();
        if (::SSL_CTX_set_cipher_list(ctx_, ciphers.c_str()) != 1) {
            throw_last_error("SSL_CTX_set_cipher_list");
        }
    }

    auto context::set_options(std::uint64_t options) -> std::uint64_t {
        return ::SSL_CTX_set_options(ctx_, options);
    }

    auto context::clear_options(std::uint64_t options) -> std::uint64_t {
        return ::SSL_CTX_clear_options(ctx_, options);
    }

    auto context::set_min_protocol_version(int version) -> void {
        ::ERR_clear_error();
        if (SSL_CTX_set_min_proto_version(ctx_, version) != 1) {
            throw_last_error("set_min_protocol_version");
        }
    }

    auto context::set_max_protocol_version(int version) -> void {
        ::ERR_clear_error();
        if (SSL_CTX_set_max_proto_version(ctx_, version) != 1) {
            throw_last_error("set_max_protocol_version");
        }
    }

    auto context::password_callback_impl(char* buffer, int size, int rwflag, void* userdata) -> int {
        auto* self = static_cast<context*>(userdata);
        if (self == nullptr or not self->password_cb_) return 0;
        auto password = self->password_cb_(static_cast<std::size_t>(std::max(size, 0)), rwflag != 0);
        const auto length = std::min<int>(static_cast<int>(password.size()), std::max(size - 1, 0));
        if (length > 0) {
            std::memcpy(buffer, password.data(), static_cast<std::size_t>(length));
        }
        if (size > 0) buffer[length] = '\0';
        return length;
    }
}