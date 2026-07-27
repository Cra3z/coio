// ReSharper disable CppMemberFunctionMayBeConst
#include <coio/detail/config.h>
#if COIO_HAS_IOCP
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
#include <cstring>
#include <limits>
#include <coio/asyncio/iocp_context.h>
#include <coio/asyncio/file.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep
#include "../common.h"

namespace coio {
    namespace detail {
        namespace {
            struct wsa_init_guard {
                wsa_init_guard() {
                    ::WSADATA data;
                    if (const auto error = ::WSAStartup(MAKEWORD(2, 2), &data)) [[unlikely]] {
                        throw std::system_error(error, std::system_category(), "WSAStartup");
                    }
                }

                wsa_init_guard(const wsa_init_guard&) = delete;

                auto operator= (const wsa_init_guard&) -> wsa_init_guard& = delete;

                ~wsa_init_guard() {
                    ::WSACleanup();
                }
            };

            auto wsa_init_library() -> void {
                static wsa_init_guard _{};
            }
        }
    }

    auto iocp_context::iocp_node::do_cancel() -> void {
        ::CancelIoEx(handle, this);
    }

    iocp_context::scheduler::io_object::io_object(iocp_context& ctx, ::HANDLE handle)
        : ctx_(&ctx), handle_(handle) {
        // NOTE: `handle` must be opend with `FILE_FLAG_OVERLAPPED` or `WSA_FLAG_OVERLAPPED`
        if (handle != INVALID_HANDLE_VALUE and handle != nullptr) {
            if (::CreateIoCompletionPort(handle, ctx.iocp_, 0, 0) == nullptr) {
                throw std::system_error{detail::to_error_code(::GetLastError()), "iocp_context::make_io_object"};
            }
        }
    }

    auto iocp_context::scheduler::io_object::cancel() -> void {
        if (handle_ == INVALID_HANDLE_VALUE) return;
        ::CancelIoEx(handle_, nullptr);
    }

    iocp_context::scheduler::file_object::file_object(iocp_context& ctx, ::HANDLE handle)
        : io_object(ctx, handle) {
        if (handle != INVALID_HANDLE_VALUE and handle != nullptr) {
            if (::LARGE_INTEGER current{}; ::SetFilePointerEx(handle, {}, &current, FILE_CURRENT)) {
                offset_ = static_cast<std::size_t>(current.QuadPart);
            }
        }
    }

    iocp_context::scheduler::file_object::~file_object() {
        close();
    }

    auto iocp_context::scheduler::file_object::close() -> void {
        const auto handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        offset_ = 0;
        if (handle == INVALID_HANDLE_VALUE or handle == nullptr) return;
        ::CancelIoEx(handle, nullptr);
        detail::throw_win_error(::CloseHandle(handle), "close");
    }

    auto iocp_context::scheduler::file_object::resize(std::size_t new_size) -> void {
        detail::throw_win_error(::SetFilePointerEx(handle_, {.QuadPart = ::LONGLONG(new_size)}, nullptr, FILE_BEGIN), "resize");
        detail::throw_win_error(::SetEndOfFile(handle_), "resize");
        detail::throw_win_error(::SetFilePointerEx(handle_, {.QuadPart = ::LONGLONG(offset_)}, nullptr, FILE_BEGIN), "resize");
    }

    auto iocp_context::scheduler::file_object::seek(std::size_t offset, detail::seek_whence whence) -> std::size_t {
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), "seek"};
        }
        if (offset > static_cast<std::size_t>(std::numeric_limits<::LONGLONG>::max())) {
            throw std::system_error{std::make_error_code(std::errc::value_too_large), "seek"};
        }

        ::DWORD method;
        switch (whence)
        {
        case detail::seek_whence::seek_set:
            method = FILE_BEGIN;
            break;
        case detail::seek_whence::seek_cur:
            method = FILE_BEGIN;
            offset = offset_ + offset;
            break;
        case detail::seek_whence::seek_end:
            method = FILE_END;
            break;
        default: unreachable();
        }

        ::LARGE_INTEGER new_offset{};
        detail::throw_win_error(::SetFilePointerEx(handle_, {.QuadPart = ::LONGLONG(offset)}, &new_offset, method), "seek");
        return offset_ = static_cast<std::size_t>(new_offset.QuadPart);
    }

    auto iocp_context::scheduler::file_object::read_some(std::span<std::byte> buffer) -> std::size_t {
        const auto n = detail::file_read_at(handle_, offset_, buffer);
        offset_ += n;
        return n;
    }

    auto iocp_context::scheduler::file_object::write_some(std::span<const std::byte> buffer) -> std::size_t {
        const auto n = detail::file_write_at(handle_, offset_, buffer);
        offset_ += n;
        return n;
    }

    auto iocp_context::scheduler::file_object::read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t {
        return detail::file_read_at(handle_, offset, buffer);
    }

    auto iocp_context::scheduler::file_object::write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t {
        return detail::file_write_at(handle_, offset, buffer);
    }

    iocp_context::scheduler::socket_object::socket_object(iocp_context& ctx, detail::socket_native_handle_type sock)
        : io_object(ctx, std::bit_cast<::HANDLE>(sock)) {}

    iocp_context::scheduler::socket_object::~socket_object() {
        close();
    }

    auto iocp_context::scheduler::socket_object::close() -> void {
        const auto handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (handle == INVALID_HANDLE_VALUE or handle == nullptr) return;
        ::CancelIoEx(handle, nullptr);
        detail::throw_wsa_error(::closesocket(std::bit_cast<::SOCKET>(handle)), "close");
    }

    auto iocp_context::scheduler::socket_object::receive(std::span<std::byte> buffer) -> std::size_t {
        return detail::socket::receive(std::bit_cast<::SOCKET>(handle_), buffer);
    }

    auto iocp_context::scheduler::socket_object::send(std::span<const std::byte> buffer) -> std::size_t {
        return detail::socket::send(std::bit_cast<::SOCKET>(handle_), buffer);
    }

    auto iocp_context::scheduler::socket_object::receive_from(std::span<std::byte> buffer) -> std::pair<endpoint, std::size_t> {
        return detail::socket::receive_from(std::bit_cast<::SOCKET>(handle_), buffer);
    }

    auto iocp_context::scheduler::socket_object::send_to(std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t {
        return detail::socket::send_to(std::bit_cast<::SOCKET>(handle_), buffer, dest);
    }

    auto iocp_context::scheduler::make_io_object(HANDLE handle) const -> file_object try {
        return file_object{*ctx_, handle};
    }
    catch (...) {
        ::CloseHandle(handle);
        throw;
    }

    auto iocp_context::scheduler::make_io_object(::SOCKET sock) const -> socket_object try {
        return socket_object{*ctx_, sock};
    }
    catch (...) {
        ::closesocket(sock);
        throw;
    }

    iocp_context::iocp_context(std::pmr::memory_resource& memory_resource)
        : loop_base(memory_resource) {
        detail::wsa_init_library();
        iocp_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (iocp_ == nullptr) {
            throw std::system_error(detail::to_error_code(::GetLastError()));
        }
    }

    iocp_context::~iocp_context() {
        ::CloseHandle(iocp_);
    }

    auto iocp_context::do_one(bool infinite) -> bool {
        if (work_count_ == 0) return false;

        while (work_count_ > 0) {
            if (consume()) {
                return true;
            }

            long long timeout = infinite ? INFINITE : 0;
            if (infinite) {
                if (const auto earliest = timer_queue_.earliest()) {
                    const auto duration = *earliest - std::chrono::steady_clock::now();
                    auto ms = std::chrono::ceil<std::chrono::milliseconds>(duration).count();
                    timeout = std::clamp(ms, 0ll, 0xff'ff'ff'fell);
                }
            }

            ::OVERLAPPED* overlapped = nullptr;
            ::ULONG_PTR key = 0;
            ::DWORD bytes = 0;
            const ::BOOL success = ::GetQueuedCompletionStatus(iocp_, &bytes, &key, &overlapped, static_cast<::DWORD>(timeout));
            const ::DWORD err = success ? 0 : ::GetLastError();

            if (not success and overlapped == nullptr and err != WAIT_TIMEOUT) [[unlikely]] {
                throw std::system_error{detail::to_error_code(err), "GetQueuedCompletionStatus"};
            }

            detail::intrusive_list<node> ready_time_ops{&node::next_}, ready_io_ops{&node::next_};
            timer_queue_.take_ready_timers(ready_time_ops);

            if (overlapped and key != wake_completion_key) {
                auto op = static_cast<iocp_node*>(overlapped);
                op->complete(bytes, err);
                ready_io_ops.push_back(*op);
            }

            publish_pending(ready_time_ops.release());
            publish_pending(ready_io_ops.release());

            if (not infinite) {
                return consume();
            }
        }

        return false;
    }

    auto iocp_context::interrupt() -> void {
        ::PostQueuedCompletionStatus(iocp_, 0, wake_completion_key, nullptr);
    }


    namespace detail {
        namespace {
            auto span_to_wsabuf(std::span<std::byte> buffer) noexcept -> ::WSABUF {
                return {
                    static_cast<::ULONG>(std::min<std::size_t>(buffer.size(), ULONG_MAX)),
                    reinterpret_cast<::CHAR*>(buffer.data())
                };
            }

            auto span_to_wsabuf(std::span<const std::byte> buffer) noexcept -> ::WSABUF {
                return span_to_wsabuf(std::span{const_cast<std::byte*>(buffer.data()), buffer.size()});
            }
        }

        // TODO: Support asynchronous operations for files which use `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` as notification mode

        /// async_read_some_at
        auto iocp_state_base_for<read_some_at_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            if (buffer_.empty()) [[unlikely]] {
                result.set_value(0);
                return start_result::completed;
            }

            ::DWORD bytes_read = 0;
            Offset = static_cast<::DWORD>(offset_ & 0xff'ff'ff'ffu);
            OffsetHigh = static_cast<::DWORD>(offset_ >> 32u);
            const ::BOOL ok = ::ReadFile(
                handle,
                buffer_.data(),
                static_cast<::DWORD>(std::min<std::size_t>(buffer_.size(), 0xff'ff'ff'ffu)),
                &bytes_read,
                this
            );
            if (not ok) {
                const ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return start_result::pending;
                else {
                    complete(0, err);
                    return start_result::completed;
                }
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<read_some_at_tag>::complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else if (error == ERROR_HANDLE_EOF or error == ERROR_BROKEN_PIPE) result.set_value(0);
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes_transferred);
            }
        }

        /// async_write_some_at
        auto iocp_state_base_for<write_some_at_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            if (buffer_.empty()) [[unlikely]] {
                result.set_value(0);
                return start_result::completed;
            }

            ::DWORD bytes_written = 0;
            Offset = static_cast<::DWORD>(offset_ & 0xff'ff'ff'ffu);
            OffsetHigh = static_cast<::DWORD>(offset_ >> 32u);
            const ::BOOL ok = ::WriteFile(
                handle,
                buffer_.data(),
                static_cast<::DWORD>(std::min<std::size_t>(buffer_.size(), 0xff'ff'ff'ffu)),
                &bytes_written,
                this
            );
            if (not ok) {
                const ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return start_result::pending;
                complete(0, err);
                return start_result::completed;
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<write_some_at_tag>::complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes_transferred);
            }
        }

        /// async_receive
        auto iocp_state_base_for<receive_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            if (buffer_.empty()) [[unlikely]] {
                result.set_value(0);
                return start_result::completed;
            }

            ::WSABUF wsabuf = span_to_wsabuf(buffer_);
            ::DWORD bytes_received = 0;
            ::DWORD flags = 0;
            const int rc = ::WSARecv(
                std::bit_cast<::SOCKET>(handle),
                &wsabuf,
                1,
                &bytes_received,
                &flags,
                this,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return start_result::pending;
                complete(0, static_cast<::DWORD>(err));
                return start_result::completed;
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<receive_tag>::complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) {
                    result.set_stopped();
                    return;
                }
                if (error == ERROR_NETNAME_DELETED) error = WSAECONNRESET;
                else if (error == ERROR_PORT_UNREACHABLE) error = WSAECONNREFUSED;
                result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes_transferred);
            }
        }

        /// async_send
        auto iocp_state_base_for<send_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            if (buffer_.empty()) [[unlikely]] {
                result.set_value(0);
                return start_result::completed;
            }

            ::WSABUF wsabuf = span_to_wsabuf(buffer_);
            ::DWORD bytes_sent = 0;
            const int rc = ::WSASend(
                std::bit_cast<::SOCKET>(handle),
                &wsabuf,
                1,
                &bytes_sent,
                0,
                this,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return start_result::pending;
                complete(0, static_cast<::DWORD>(err));
                return start_result::completed;
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<send_tag>::complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) {
                    result.set_stopped();
                    return;
                }
                if (error == ERROR_NETNAME_DELETED) error = WSAECONNRESET;
                else if (error == ERROR_PORT_UNREACHABLE) error = WSAECONNREFUSED;
                result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes_transferred);
            }
        }

        /// async_receive_from
        auto iocp_state_base_for<receive_from_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }

            std::memset(&peer_storage_, 0, sizeof(peer_storage_));
            peer_length_ = sizeof(::sockaddr_storage);
            ::WSABUF wsabuf = span_to_wsabuf(buffer_);
            ::DWORD bytes_received = 0;
            ::DWORD flags = 0;
            const int rc = ::WSARecvFrom(
                std::bit_cast<::SOCKET>(handle),
                &wsabuf,
                1,
                &bytes_received,
                &flags,
                reinterpret_cast<SOCKADDR*>(&peer_storage_),
                &peer_length_,
                this,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return start_result::pending;
                complete(0, static_cast<::DWORD>(err));
                return start_result::completed;
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<receive_from_tag>::complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) {
                    result.set_stopped();
                    return;
                }
                if (error == ERROR_NETNAME_DELETED) error = WSAECONNRESET;
                else if (error == ERROR_PORT_UNREACHABLE) error = WSAECONNREFUSED;
                result.set_error(to_error_code(error));
            }
            else {
                result.set_value(sockaddr_storage_to_endpoint(peer_storage_), bytes_transferred);
            }
        }

        /// async_send_to
        auto iocp_state_base_for<send_to_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }

            ::WSABUF wsabuf = span_to_wsabuf(buffer_);
            ::DWORD bytes_sent = 0;
            auto sa = endpoint_to_sockaddr_in(dest_);
            auto [psa, len] = to_sockaddr(sa);
            const int rc = ::WSASendTo(
                std::bit_cast<::SOCKET>(handle),
                &wsabuf,
                1,
                &bytes_sent,
                0,
                psa,
                len,
                this,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return start_result::pending;
                complete(0, static_cast<::DWORD>(err));
                return start_result::completed;
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<send_to_tag>::complete(::DWORD bytes_transferred, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) {
                    result.set_stopped();
                    return;
                }
                if (error == ERROR_NETNAME_DELETED) error = WSAECONNRESET;
                else if (error == ERROR_PORT_UNREACHABLE) error = WSAECONNREFUSED;
                result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes_transferred);
            }
        }

        /// async_accept
        auto iocp_state_base_for<accept_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }
            const auto sock = std::bit_cast<::SOCKET>(handle);

            ::WSAPROTOCOL_INFOW info{};
            int info_length = sizeof(info);
            if (::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFO, reinterpret_cast<char*>(&info), &info_length) == SOCKET_ERROR) {
                result.set_error(to_error_code(::WSAGetLastError()));
                return start_result::completed;
            }

            accepted_ = ::WSASocketW(
                info.iAddressFamily,
                info.iSocketType,
                info.iProtocol,
                nullptr,
                0,
                WSA_FLAG_OVERLAPPED
            );

            if (accepted_ == INVALID_SOCKET) {
                result.set_error(to_error_code(static_cast<::DWORD>(::WSAGetLastError())));
                return start_result::completed;
            }

            ::DWORD bytes_received = 0;
            const ::BOOL ok = ::AcceptEx(
                sock,
                accepted_,
                output_buffer_,
                0u,
                sizeof(::sockaddr_storage) + 16u,
                sizeof(::sockaddr_storage) + 16u,
                &bytes_received,
                this
            );
            if (not ok) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return start_result::pending;
                complete(0, static_cast<::DWORD>(err));
                return start_result::completed;
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<accept_tag>::complete(::DWORD, ::DWORD error) noexcept -> void {
            if (error) {
                ::closesocket(std::exchange(accepted_, INVALID_SOCKET));
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
                return;
            }
            if (::setsockopt(
                accepted_,
                SOL_SOCKET,
                SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<const char*>(&handle),
                sizeof(handle)
            ) == SOCKET_ERROR) [[unlikely]] {
                const auto err = static_cast<::DWORD>(::WSAGetLastError());
                ::closesocket(std::exchange(accepted_, INVALID_SOCKET));
                result.set_error(to_error_code(err));
                return;
            }
            result.set_value(accepted_);
        }

        /// async_connect
        auto iocp_state_base_for<connect_tag>::do_start() noexcept -> start_result {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return start_result::completed;
            }

            const auto sock = std::bit_cast<::SOCKET>(handle);
            ::LPFN_CONNECTEX ConnectEx = nullptr;
            ::GUID connectex_guid = WSAID_CONNECTEX;
            ::DWORD byte_count = 0;

            if (::WSAIoctl(
                sock,
                SIO_GET_EXTENSION_FUNCTION_POINTER,
                &connectex_guid, sizeof(connectex_guid),
                &ConnectEx, sizeof(ConnectEx),
                &byte_count, nullptr, nullptr) == SOCKET_ERROR)
            {
                result.set_error(to_error_code(::WSAGetLastError()));
                return start_result::completed;
            }

            {
                ::WSAPROTOCOL_INFOW info{};
                int info_length = sizeof(info);
                if (::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFO, reinterpret_cast<char*>(&info), &info_length) != 0) {
                    result.set_error(to_error_code(::WSAGetLastError()));
                    return start_result::completed;
                }

                ::DWORD err = 0;
                if (info.iAddressFamily == AF_INET) {
                    ::sockaddr_in addr4 = {
                        .sin_family = AF_INET,
                        .sin_port = 0,
                        .sin_addr = in4addr_any
                    };
                    if (::bind(sock, reinterpret_cast<::sockaddr*>(&addr4), sizeof(addr4)) == SOCKET_ERROR) {
                        err = static_cast<::DWORD>(::WSAGetLastError());
                    }
                }
                else if (info.iAddressFamily == AF_INET6) {
                    ::sockaddr_in6 addr6 = {
                        .sin6_family = AF_INET6,
                        .sin6_port = 0,
                        .sin6_addr = in6addr_any
                    };
                    if (::bind(sock, reinterpret_cast<::sockaddr*>(&addr6), sizeof(addr6)) == SOCKET_ERROR) {
                        err = static_cast<::DWORD>(::WSAGetLastError());
                    }
                }
                else {
                    err = WSAEAFNOSUPPORT;
                }
                if (err and err != WSAEINVAL) {
                    result.set_error(to_error_code(err));
                    return start_result::completed;
                }
            }

            auto sa = endpoint_to_sockaddr_in(peer_);
            auto [psa, len] = to_sockaddr(sa);
            const ::BOOL ok = ConnectEx(
                sock,
                psa,
                len,
                nullptr,
                0,
                nullptr,
                this
            );
            if (not ok) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return start_result::pending;
                complete(0, static_cast<::DWORD>(err));
                return start_result::completed;
            }
            return start_result::pending;
        }

        auto iocp_state_base_for<connect_tag>::complete(::DWORD, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
                return;
            }
            if (::setsockopt(
                std::bit_cast<::SOCKET>(handle),
                SOL_SOCKET,
                SO_UPDATE_CONNECT_CONTEXT,
                nullptr,
                0
            ) == SOCKET_ERROR) [[unlikely]] {
                result.set_error(to_error_code(static_cast<::DWORD>(::WSAGetLastError())));
                return;
            }
            result.set_value();
        }
    }
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep

#endif
