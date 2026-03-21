#include <array>
#include <cerrno> // IWYU pragma: keep
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <coio/detail/error.h>
#include <coio/ssl/stream.h>

namespace coio::ssl::detail {
    namespace {
        constexpr std::size_t transfer_buffer_size = 1024;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto last_ERR_error_code() noexcept -> std::error_code {
            return {static_cast<int>(::ERR_get_error()), error::ssl_category()};
        }

        [[nodiscard]]
        auto ssl_failure_code(int ssl_error) noexcept -> std::error_code {
            if (const auto ec = last_ERR_error_code()) return ec;
            if (ssl_error == SSL_ERROR_ZERO_RETURN) return coio::error::make_error_code(coio::error::eof);
            if (ssl_error == SSL_ERROR_SYSCALL) {
#if COIO_OS_WINDOWS
                return {static_cast<int>(::GetLastError()), std::system_category()};
#else
                return {errno, std::system_category()};
#endif
            }
            return std::make_error_code(std::errc::io_error);
        }

        [[noreturn]]
        auto throw_ssl_failure(int ssl_error, const char* what) -> void {
            throw std::system_error{ssl_failure_code(ssl_error), what};
        }
    }

    stream_base::stream_base(context& ctx) {
        ::ERR_clear_error();

        ssl_ = ::SSL_new(ctx.native_handle());
        if (ssl_ == nullptr) [[unlikely]] {
            throw std::system_error{last_ERR_error_code(), "ssl::stream"};
        }

        const auto rbio = ::BIO_new(::BIO_s_mem());
        if (rbio == nullptr) [[unlikely]] {
            throw std::system_error{last_ERR_error_code(), "ssl::stream"};
        }

        const auto wbio = ::BIO_new(::BIO_s_mem());
        if (wbio == nullptr) [[unlikely]] {
            ::BIO_free(rbio);
            throw std::system_error{last_ERR_error_code(), "ssl::stream"};
        }

        BIO_set_mem_eof_return(rbio, -1);
        BIO_set_mem_eof_return(wbio, -1);
        ::SSL_set_bio(ssl_, rbio, wbio);
        SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_RELEASE_BUFFERS);
    }

    stream_base::~stream_base() {
        if (ssl_) ::SSL_free(ssl_);
    }

    auto stream_base::set_verify_mode(verify_mode mode) -> void {
        ::SSL_set_verify(ssl_, to_underlying(mode), nullptr);
    }

    auto stream_base::set_verify_depth(int depth) -> void {
        ::SSL_set_verify_depth(ssl_, depth);
    }

    auto stream_base::set_server_name(zstring_view host) -> void {
        ::ERR_clear_error();
        if (::SSL_set_tlsext_host_name(ssl_, host.c_str()) != 1) {
            throw std::system_error{last_ERR_error_code(), "set_server_name"};
        }
    }

    auto stream_base::set_verify_host_name(zstring_view host) -> void {
        ::ERR_clear_error();
        if (::SSL_set1_host(ssl_, host.c_str()) != 1) {
            throw std::system_error{last_ERR_error_code(), "set_verify_host_name"};
        }
    }

    auto stream_base::set_host_name(zstring_view host) -> void {
        set_server_name(host);
        set_verify_host_name(host);
    }

    auto stream_base::handshake(handshake_type type) -> void {
        set_handshake_state(type);
        for (;;) {
            flush_output();
            ::ERR_clear_error();
            const int rc = ::SSL_do_handshake(ssl_);
            if (rc == 1) {
                flush_output();
                return;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                flush_output();
                fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                flush_output();
                continue;
            }
            throw_ssl_failure(ssl_error, "SSL_do_handshake");
        }
    }

    auto stream_base::async_handshake(handshake_type type) -> task<> {
        set_handshake_state(type);
        for (;;) {
            co_await async_flush_output();
            ::ERR_clear_error();
            const int rc = ::SSL_do_handshake(ssl_);
            if (rc == 1) {
                co_await async_flush_output();
                co_return;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                co_await async_flush_output();
                co_await async_fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                co_await async_flush_output();
                continue;
            }
            throw_ssl_failure(ssl_error, "SSL_do_handshake");
        }
    }

    auto stream_base::shutdown() -> void {
        for (;;) {
            flush_output();
            ::ERR_clear_error();
            const int rc = ::SSL_shutdown(ssl_);
            if (rc == 1) {
                flush_output();
                return;
            }
            if (rc == 0) {
                flush_output();
                fill_input();
                continue;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                flush_output();
                fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                flush_output();
                continue;
            }
            if (ssl_error == SSL_ERROR_ZERO_RETURN) return;
            throw_ssl_failure(ssl_error, "SSL_shutdown");
        }
    }

    auto stream_base::async_shutdown() -> task<> {
        for (;;) {
            co_await async_flush_output();
            ::ERR_clear_error();
            const int rc = ::SSL_shutdown(ssl_);
            if (rc == 1) {
                co_await async_flush_output();
                co_return;
            }
            if (rc == 0) {
                co_await async_flush_output();
                co_await async_fill_input();
                continue;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                co_await async_flush_output();
                co_await async_fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                co_await async_flush_output();
                continue;
            }
            if (ssl_error == SSL_ERROR_ZERO_RETURN) co_return;
            throw_ssl_failure(ssl_error, "SSL_shutdown");
        }
    }

    auto stream_base::read_some(std::span<std::byte> buffer) -> std::size_t {
        if (buffer.empty()) return 0;
        for (;;) {
            flush_output();
            std::size_t bytes_transferred = 0;
            ::ERR_clear_error();
            const int rc = ::SSL_read_ex(ssl_, buffer.data(), buffer.size(), &bytes_transferred);
            if (rc == 1) {
                flush_output();
                return bytes_transferred;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                flush_output();
                fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                flush_output();
                continue;
            }
            throw_ssl_failure(ssl_error, "SSL_read_ex");
        }
    }

    auto stream_base::write_some(std::span<const std::byte> buffer) -> std::size_t {
        if (buffer.empty()) return 0;
        for (;;) {
            flush_output();
            std::size_t bytes_transferred = 0;
            ::ERR_clear_error();
            const int rc = ::SSL_write_ex(ssl_, buffer.data(), buffer.size(), &bytes_transferred);
            if (rc == 1) {
                flush_output();
                return bytes_transferred;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                flush_output();
                fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                flush_output();
                continue;
            }
            throw_ssl_failure(ssl_error, "SSL_write_ex");
        }
    }

    auto stream_base::async_read_some_impl(std::span<std::byte> buffer) -> task<std::size_t> {
        if (buffer.empty()) co_return 0;
        for (;;) {
            co_await async_flush_output();
            std::size_t bytes_transferred = 0;
            ::ERR_clear_error();
            const int rc = ::SSL_read_ex(ssl_, buffer.data(), buffer.size(), &bytes_transferred);
            if (rc == 1) {
                co_await async_flush_output();
                co_return bytes_transferred;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                co_await async_flush_output();
                co_await async_fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                co_await async_flush_output();
                continue;
            }
            throw_ssl_failure(ssl_error, "SSL_read_ex");
        }
    }

    auto stream_base::async_write_some_impl(std::span<const std::byte> buffer) -> task<std::size_t> {
        if (buffer.empty()) co_return 0;
        for (;;) {
            co_await async_flush_output();
            std::size_t bytes_transferred = 0;
            ::ERR_clear_error();
            const int rc = ::SSL_write_ex(ssl_, buffer.data(), buffer.size(), &bytes_transferred);
            if (rc == 1) {
                co_await async_flush_output();
                co_return bytes_transferred;
            }
            const int ssl_error = ::SSL_get_error(ssl_, rc);
            if (ssl_error == SSL_ERROR_WANT_READ) {
                co_await async_flush_output();
                co_await async_fill_input();
                continue;
            }
            if (ssl_error == SSL_ERROR_WANT_WRITE) {
                co_await async_flush_output();
                continue;
            }
            throw_ssl_failure(ssl_error, "SSL_write_ex");
        }
    }

    auto stream_base::set_handshake_state(handshake_type type) -> void {
        if (type == handshake_type::client) ::SSL_set_connect_state(ssl_);
        else ::SSL_set_accept_state(ssl_);
    }

    auto stream_base::flush_output() -> void {
        std::byte buffer[transfer_buffer_size];
        const auto wbio = ::SSL_get_wbio(ssl_);
        while (::BIO_ctrl_pending(wbio) > 0) {
            ::ERR_clear_error();
            const int rc = ::BIO_read(wbio, buffer, std::ranges::size(buffer));
            if (rc <= 0) {
                if (::BIO_should_retry(wbio)) continue;
                throw std::system_error{last_ERR_error_code(), "BIO_read"};
            }
            std::span<const std::byte> view{buffer, static_cast<std::size_t>(rc)};
            while (not view.empty()) {
                const auto transferred = transport_write_some(view);
                view = view.subspan(transferred);
            }
        }
    }

    auto stream_base::async_flush_output() -> task<> {
        std::byte buffer[transfer_buffer_size];
        const auto wbio = ::SSL_get_wbio(ssl_);
        while (::BIO_ctrl_pending(wbio) > 0) {
            ::ERR_clear_error();
            const int rc = ::BIO_read(wbio, buffer, std::ranges::size(buffer));
            if (rc <= 0) {
                if (::BIO_should_retry(wbio)) continue;
                throw std::system_error{last_ERR_error_code(), "BIO_read"};
            }
            std::span<const std::byte> view{buffer, static_cast<std::size_t>(rc)};
            while (not view.empty()) {
                const auto transferred = co_await async_transport_write_some(view);
                view = view.subspan(transferred);
            }
        }
    }

    auto stream_base::fill_input() -> void {
        std::byte buffer[transfer_buffer_size];
        const auto bytes_read = transport_read_some(buffer);
        push_input_bytes({buffer, bytes_read});
    }

    auto stream_base::async_fill_input() -> task<> {
        std::byte buffer[transfer_buffer_size];
        const auto bytes_read = co_await async_transport_read_some(buffer);
        push_input_bytes({buffer, bytes_read});
    }

    auto stream_base::push_input_bytes(std::span<const std::byte> buffer) -> void {
        const auto rbio = ::SSL_get_rbio(ssl_);
        while (not buffer.empty()) {
            ::ERR_clear_error();
            const int rc = ::BIO_write(rbio, buffer.data(), static_cast<int>(buffer.size()));
            if (rc <= 0) {
                if (::BIO_should_retry(rbio)) continue;
                throw std::system_error{last_ERR_error_code(), "BIO_write"};
            }
            buffer = buffer.subspan(static_cast<std::size_t>(rc));
        }
    }
}