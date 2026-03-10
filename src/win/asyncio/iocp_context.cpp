// ReSharper disable CppMemberFunctionMayBeConst
#include <coio/detail/config.h>
#if COIO_HAS_IOCP
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
#include <coio/asyncio/iocp_context.h>
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
        ::CancelIoEx(context_.iocp_, this);
    }

    iocp_context::scheduler::io_object::io_object(iocp_context& ctx, HANDLE handle)
        : ctx_(&ctx), handle_(handle) {
        if (handle != INVALID_HANDLE_VALUE and handle != nullptr) {
            ::CreateIoCompletionPort(handle, ctx.iocp_, 0, 0);
        }
    }

    iocp_context::scheduler::io_object::~io_object() {
        cancel();
    }

    auto iocp_context::scheduler::io_object::cancel() -> void {
        if (handle_ == INVALID_HANDLE_VALUE) return;
        ::CancelIoEx(handle_, nullptr);
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
        request_stop();
        ::CloseHandle(iocp_);
    }

    auto iocp_context::do_one(bool infinite) -> bool {
        if (work_count_ == 0) return false;

        while (work_count_ > 0) {
            if (const auto op = op_queue_.try_dequeue()) {
                op->finish();
                return true;
            }

            std::unique_lock lock{bolt_, std::try_to_lock};
            if (not lock) {
                std::this_thread::yield();
                continue;
            }

            if (work_count_ == 0) break;

            ::DWORD timeout = infinite ? INFINITE : 0;
            if (infinite) {
                using milliseconds = std::chrono::duration<::DWORD, std::milli>;
                if (const auto earliest = timer_queue_.earliest()) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto ms = std::chrono::duration_cast<milliseconds>(*earliest - now).count();
                    timeout = static_cast<::DWORD>(std::max<long long>(ms, 0LL));
                    if (timeout > 0) timeout += 1;
                }
            }

            ::OVERLAPPED* overlapped = nullptr;
            ::ULONG_PTR key = 0;
            ::DWORD bytes = 0;
            const ::BOOL success = ::GetQueuedCompletionStatus(iocp_, &bytes, &key, &overlapped, timeout);
            const ::DWORD err = success ? 0 : ::GetLastError();

            op_queue local_ops;
            timer_queue_.take_ready_timers(local_ops);

            lock.unlock();

            if (overlapped == nullptr or key == wake_completion_key) {
                op_queue_.splice(std::move(local_ops));
                if (not infinite) {
                    const auto op = op_queue_.try_dequeue();
                    if (op) op->finish();
                    return op != nullptr;
                }
                continue;
            }

            auto inode = static_cast<iocp_node*>(overlapped);
            inode->next_ = nullptr;
            inode->complete(bytes, err);
            local_ops.unsynchronized_enqueue(*inode);

            op_queue_.splice(std::move(local_ops));

            if (not infinite) {
                const auto op = op_queue_.try_dequeue();
                if (op) op->finish();
                return op != nullptr;
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

        /// async_read_some
        template<>
        auto iocp_state_base_for<async_read_some_t>::do_start() noexcept -> bool { // TODO: stream_file
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                immediately_post();
                return true;
            }

            ::DWORD bytes_read = 0;
            const ::BOOL ok = ::ReadFile(
                handle,
                buffer.data(),
                std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu),
                &bytes_read,
                this
            );
            if (not ok) {
                const ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                if (err == ERROR_HANDLE_EOF) {}
                else {
                    complete(0, err);
                    return false;
                }
            }
            complete(bytes_read, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_read_some_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) {
                    result.set_stopped();
                }
                else if (error == ERROR_HANDLE_EOF) {
                    result.set_value(0);
                }
                else {
                    result.set_error(to_error_code(error));
                }
            }
            else {
                result.set_value(bytes);
            }
        }

        /// async_write_some
        template<>
        auto iocp_state_base_for<async_write_some_t>::do_start() noexcept -> bool { // TODO: stream_file
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                immediately_post();
                return true;
            }

            ::DWORD bytes_written = 0;
            const ::BOOL ok = ::WriteFile(
                handle,
                buffer.data(),
                std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu),
                &bytes_written,
                this
            );
            if (not ok) {
                const ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(bytes_written, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_write_some_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes);
            }
        }

        /// async_read_some_at
        template<>
        auto iocp_state_base_for<async_read_some_at_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                immediately_post();
                return true;
            }

            ::DWORD bytes_read = 0;
            Offset = static_cast<::DWORD>(offset & 0xff'ff'ff'ffu);
            OffsetHigh = static_cast<::DWORD>(offset >> 32u);
            const ::BOOL ok = ::ReadFile(
                handle,
                buffer.data(),
                std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu),
                &bytes_read,
                this
            );
            if (not ok) {
                const ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                if (err == ERROR_HANDLE_EOF) {}
                else {
                    complete(0, err);
                    return false;
                }
            }
            complete(bytes_read, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_read_some_at_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else if (error == ERROR_HANDLE_EOF) result.set_value(0);
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes);
            }
        }

        /// async_write_some_at
        template<>
        auto iocp_state_base_for<async_write_some_at_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                immediately_post();
                return true;
            }
            
            ::DWORD bytes_written = 0;
            Offset = static_cast<::DWORD>(offset & 0xff'ff'ff'ffu);
            OffsetHigh = static_cast<::DWORD>(offset >> 32u);
            const ::BOOL ok = ::WriteFile(
                handle,
                buffer.data(),
                std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu),
                &bytes_written,
                this
            );
            if (not ok) {
                const ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(bytes_written, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_write_some_at_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(bytes);
            }
        }

        /// async_receive
        template<>
        auto iocp_state_base_for<async_receive_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                immediately_post();
                return true;
            }

            ::WSABUF wsabuf = span_to_wsabuf(buffer);
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
                if (err == WSA_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(bytes_received, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_receive_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(static_cast<std::size_t>(bytes));
            }
        }

        /// async_send
        template<>
        auto iocp_state_base_for<async_send_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            if (buffer.empty()) [[unlikely]] {
                result.set_value(0);
                immediately_post();
                return true;
            }
            
            ::WSABUF wsabuf = span_to_wsabuf(buffer);
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
                if (err == WSA_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(bytes_sent, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_send_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(static_cast<std::size_t>(bytes));
            }
        }

        /// async_receive_from
        template<>
        auto iocp_state_base_for<async_receive_from_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            
            std::memset(&peer_storage, 0, sizeof(peer_storage));
            peer_length = sizeof(::sockaddr_storage);
            ::WSABUF wsabuf = span_to_wsabuf(buffer);
            ::DWORD bytes_received = 0;
            ::DWORD flags = 0;
            const int rc = ::WSARecvFrom(
                std::bit_cast<::SOCKET>(handle),
                &wsabuf,
                1,
                &bytes_received,
                &flags,
                reinterpret_cast<SOCKADDR*>(&peer_storage),
                &peer_length,
                this,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(bytes_received, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_receive_from_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(static_cast<std::size_t>(bytes));
            }
        }

        /// async_send_to
        template<>
        auto iocp_state_base_for<async_send_to_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            
            ::WSABUF wsabuf = span_to_wsabuf(buffer);
            ::DWORD bytes_sent = 0;
            auto sa = endpoint_to_sockaddr_in(peer);
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
                if (err == WSA_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(bytes_sent, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_send_to_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
            }
            else {
                result.set_value(static_cast<std::size_t>(bytes));
            }
        }

        /// async_accept
        template<>
        auto iocp_state_base_for<async_accept_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            const auto sock = std::bit_cast<::SOCKET>(handle);

            ::WSAPROTOCOL_INFOW info{};
            int info_length = sizeof(info);
            if (::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFO, reinterpret_cast<char*>(&info), &info_length) == SOCKET_ERROR) {
                result.set_error(to_error_code(::WSAGetLastError()));
                return false;
            }

            accepted = ::WSASocketW(
                info.iAddressFamily,
                info.iSocketType,
                info.iProtocol,
                nullptr,
                0,
                WSA_FLAG_OVERLAPPED
            );

            if (accepted == INVALID_SOCKET) {
                result.set_error(to_error_code(static_cast<::DWORD>(::WSAGetLastError())));
                return false;
            }
            
            ::DWORD bytes_received = 0;
            const ::BOOL ok = ::AcceptEx(
                sock,
                accepted,
                output_buffer,
                0u,
                sizeof(::sockaddr_storage) + 16u,
                sizeof(::sockaddr_storage) + 16u,
                &bytes_received,
                this
            );
            if (not ok) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(0, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_accept_t>::complete(::DWORD, ::DWORD error) noexcept -> void {
            if (error) {
                ::closesocket(std::exchange(accepted, INVALID_SOCKET));
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
                return;
            }
            ::setsockopt(
                accepted,
                SOL_SOCKET,
                SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<const char*>(&handle),
                sizeof(handle)
            );
            result.set_value(accepted);
        }

        /// async_connect
        template<>
        auto iocp_state_base_for<async_connect_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }

            ::LPFN_CONNECTEX ConnectEx = nullptr;
            ::GUID connectex_guid = WSAID_CONNECTEX;
            ::DWORD byteCount = 0;
            if (::WSAIoctl(
                std::bit_cast<::SOCKET>(handle),
                SIO_GET_EXTENSION_FUNCTION_POINTER,
                &connectex_guid, sizeof(connectex_guid),
                &ConnectEx, sizeof(ConnectEx),
                &byteCount, nullptr, nullptr) == SOCKET_ERROR)
            {
                result.set_error(to_error_code(::WSAGetLastError()));
                return false;
            }

            auto sa = endpoint_to_sockaddr_in(peer);
            auto [psa, len] = to_sockaddr(sa);
            const ::BOOL ok = ConnectEx(
                std::bit_cast<::SOCKET>(handle),
                psa,
                len,
                nullptr,
                0,
                nullptr,
                this
            );
            if (not ok) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
            complete(0, 0);
            immediately_post();
            return true;
        }

        template<>
        auto iocp_state_base_for<async_connect_t>::complete(::DWORD, ::DWORD error) noexcept -> void {
            if (error) {
                if (error == ERROR_OPERATION_ABORTED) result.set_stopped();
                else result.set_error(to_error_code(error));
                return;
            }
            ::setsockopt(
                std::bit_cast<::SOCKET>(handle),
                SOL_SOCKET,
                SO_UPDATE_CONNECT_CONTEXT,
                nullptr,
                0
            );
            result.set_value();
        }
    }
}
#endif
