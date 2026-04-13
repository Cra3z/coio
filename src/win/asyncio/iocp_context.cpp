// ReSharper disable CppMemberFunctionMayBeConst
#include <coio/detail/config.h>
#if COIO_HAS_IOCP
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>
#include <coio/asyncio/iocp_context.h>
#include <coio/asyncio/file.h>
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

            struct ntdll_loader {
                using NtSetInformationFile_ = ::LONG (NTAPI*)(::HANDLE, ::ULONG_PTR*, void*, ::ULONG, ::ULONG);
                using RtlNtStatusToDosError_ = ::ULONG (NTAPI*)(::LONG);

                ntdll_loader() noexcept {
                    if (::HMODULE ntdll = ::GetModuleHandleA("NTDLL.DLL")) {
                        NtSetInformationFile = reinterpret_cast<NtSetInformationFile_>(::GetProcAddress(ntdll, "NtSetInformationFile"));
                        RtlNtStatusToDosError = reinterpret_cast<RtlNtStatusToDosError_>(::GetProcAddress(ntdll, "RtlNtStatusToDosError"));
                    }
                }
                NtSetInformationFile_ NtSetInformationFile{};
                RtlNtStatusToDosError_ RtlNtStatusToDosError{};
            };

            auto wsa_init_library() -> void {
                static wsa_init_guard _{};
            }

            auto deassociate_iocp(::HANDLE handle) -> void {
                if (handle == nullptr or handle == INVALID_HANDLE_VALUE) [[unlikely]] return;
                static constexpr ::ULONG FileReplaceCompletionInformation = 61;
                static ntdll_loader loader;
                ::ULONG_PTR block[2]{};
                void* info[2]{};
                if (loader.NtSetInformationFile == nullptr) return;
                const auto status = loader.NtSetInformationFile(handle, block, info, sizeof(info), FileReplaceCompletionInformation);
                if (status) [[unlikely]] {
                    const ::DWORD win32_err = loader.RtlNtStatusToDosError ? loader.RtlNtStatusToDosError(status) : ERROR_NOT_SUPPORTED;
                    throw std::system_error{detail::to_error_code(win32_err), "release"};
                }
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
            if (::LARGE_INTEGER current{}; ::SetFilePointerEx(handle, {}, &current, FILE_CURRENT)) {
                offset_ = static_cast<std::size_t>(current.QuadPart);
            }
            if (::CreateIoCompletionPort(handle, ctx.iocp_, 0, 0) == nullptr) {
                throw std::system_error{detail::to_error_code(::GetLastError()), "iocp_context::make_io_object"};
            }
        }
    }

    iocp_context::scheduler::io_object::~io_object() {
        cancel();
    }

    auto iocp_context::scheduler::io_object::release() -> handle_wrapper {
        if (handle_ != INVALID_HANDLE_VALUE) {
            cancel();
            detail::deassociate_iocp(handle_);
        }
        offset_ = 0;
        return handle_wrapper{std::exchange(handle_, INVALID_HANDLE_VALUE)};
    }

    auto iocp_context::scheduler::io_object::cancel() -> void {
        if (handle_ == INVALID_HANDLE_VALUE) return;
        ::CancelIoEx(handle_, nullptr);
    }

    auto iocp_context::scheduler::io_object::file_resize(std::size_t new_size) -> void {
        detail::throw_win_error(::SetFilePointerEx(handle_, {.QuadPart = ::LONGLONG(new_size)}, nullptr, FILE_BEGIN), "resize");
        detail::throw_win_error(::SetEndOfFile(handle_), "resize");
        detail::throw_win_error(::SetFilePointerEx(handle_, {.QuadPart = ::LONGLONG(offset_)}, nullptr, FILE_BEGIN), "resize");
    }

    auto iocp_context::scheduler::io_object::file_seek(std::size_t offset, detail::seek_whence whence) -> std::size_t {
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), "seek"};
        }
        if (offset > LONG_LONG_MAX) {
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
        default:
            throw std::system_error{std::make_error_code(std::errc::invalid_argument), "seek"};
        }

        ::LARGE_INTEGER new_offset{};
        detail::throw_win_error(::SetFilePointerEx(handle_, {.QuadPart = ::LONGLONG(offset)}, &new_offset, method), "seek");
        return offset_ = new_offset.QuadPart;
    }

    auto iocp_context::scheduler::io_object::file_read(std::span<std::byte> buffer) -> std::size_t {
        const auto n = detail::file_read(handle_, buffer);
        offset_ += buffer.size();
        return n;
    }

    auto iocp_context::scheduler::io_object::file_write(std::span<const std::byte> buffer) -> std::size_t {
        const auto n = detail::file_write(handle_, buffer);
        offset_ += buffer.size();
        return n;
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
                return consume(infinite);
            }

            if (work_count_ == 0) break;

            long long timeout = infinite ? INFINITE : 0;
            if (infinite) {
                if (const auto earliest = timer_queue_.earliest()) {
                    const auto duration = *earliest - std::chrono::steady_clock::now();
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                    timeout = std::clamp(ms, 0ll, 0xff'ff'ff'ffll);
                }
            }

            ::OVERLAPPED* overlapped = nullptr;
            ::ULONG_PTR key = 0;
            ::DWORD bytes = 0;
            const ::BOOL success = ::GetQueuedCompletionStatus(iocp_, &bytes, &key, &overlapped, timeout);
            const ::DWORD err = success ? 0 : ::GetLastError();

            node* ready_time_ops = nullptr;
            timer_queue_.take_ready_timers(ready_time_ops, &node::next_);

            lock.unlock();

            node* ready_io_ops = nullptr;
            if (overlapped and key != wake_completion_key) {
                auto inode = static_cast<iocp_node*>(overlapped);
                inode->complete(bytes, err);
                inode->next_ = std::exchange(ready_io_ops, inode);
            }

            if (ready_time_ops) op_queue_.enqueue(*ready_time_ops);
            if (ready_io_ops) op_queue_.enqueue(*ready_io_ops);

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

        // TODO: Support asynchronous operations for files which use `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` as notification mode

        /// async_read_some
        template<>
        auto iocp_state_base_for<async_read_some_t>::do_start() noexcept -> bool {
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
        auto iocp_state_base_for<async_write_some_t>::do_start() noexcept -> bool {
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
            return true;
        }

        template<>
        auto iocp_state_base_for<async_receive_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
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
                result.set_value(bytes);
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
            return true;
        }

        template<>
        auto iocp_state_base_for<async_send_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
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
                result.set_value(bytes);
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
            return true;
        }

        template<>
        auto iocp_state_base_for<async_receive_from_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
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
                result.set_value(sockaddr_storage_to_endpoint(peer_storage), bytes);
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
            return true;
        }

        template<>
        auto iocp_state_base_for<async_send_to_t>::complete(::DWORD bytes, ::DWORD error) noexcept -> void {
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
                result.set_value(bytes);
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
                return false;
            }

            {
                ::WSAPROTOCOL_INFOW info{};
                int info_length = sizeof(info);
                if (::getsockopt(sock, SOL_SOCKET, SO_PROTOCOL_INFO, reinterpret_cast<char*>(&info), &info_length) != 0) {
                    result.set_error(to_error_code(::WSAGetLastError()));
                    return false;
                }

                ::DWORD err = 0;
                if (info.iAddressFamily == AF_INET) {
                    ::sockaddr_in addr4 = {
                        .sin_family = AF_INET,
                        .sin_port = 0,
                        .sin_addr = in4addr_any
                    };
                    if (::bind(sock, reinterpret_cast<::sockaddr*>(&addr4), sizeof(addr4)) == SOCKET_ERROR) {
                        err = ::WSAGetLastError();
                    }
                }
                else if (info.iAddressFamily == AF_INET6) {
                    ::sockaddr_in6 addr6 = {
                        .sin6_family = AF_INET6,
                        .sin6_port = 0,
                        .sin6_addr = in6addr_any
                    };
                    if (::bind(sock, reinterpret_cast<::sockaddr*>(&addr6), sizeof(addr6)) == SOCKET_ERROR) {
                        err = ::WSAGetLastError();
                    }
                }
                else {
                    err = WSAEAFNOSUPPORT;
                }
                if (err and err != WSAEINVAL) {
                    result.set_error(to_error_code(err));
                    return false;
                }
            }

            auto sa = endpoint_to_sockaddr_in(peer);
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
                if (err == WSA_IO_PENDING) return true;
                complete(0, err);
                return false;
            }
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
