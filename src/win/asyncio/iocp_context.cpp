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
        iocp_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        if (iocp_ == nullptr) {
            throw std::system_error(detail::to_error_code());
        }
    }

    iocp_context::~iocp_context() {
        request_stop();
        ::CloseHandle(iocp_);
    }

    auto iocp_context::do_one(bool infinite) -> bool {
        if (work_count_ == 0) return false;

        while (work_count_ > 0) {
            // Fast path: already-queued completions.
            if (const auto op = op_queue_.try_dequeue()) {
                op->finish();
                return true;
            }

            std::unique_lock lock{bolt_, std::try_to_lock};
            if (!lock) {
                std::this_thread::yield();
                continue;
            }

            if (work_count_ == 0) break;

            // Compute wait duration for timers.
            DWORD timeout = infinite ? INFINITE : 0;
            if (infinite) {
                using milliseconds = std::chrono::duration<DWORD, std::milli>;
                if (const auto earliest = timer_queue_.earliest()) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto ms = std::chrono::duration_cast<milliseconds>(*earliest - now).count();
                    timeout = static_cast<DWORD>(std::max<long long>(ms, 0LL));
                    if (timeout > 0) timeout += 1; // round up to avoid spurious timeouts
                }
            }

            OVERLAPPED* ov = nullptr;
            ULONG_PTR key = 0;
            DWORD bytes = 0;
            const BOOL success = ::GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, timeout);
            const DWORD err = success ? 0 : ::GetLastError();

            // Collect any expired timers into a local queue.
            op_queue local_ops;
            timer_queue_.take_ready_timers(local_ops);

            lock.unlock();

            // Timeout (ov == nullptr) or a spurious wake from PostQueuedCompletionStatus.
            if (ov == nullptr or key == wake_completion_key) {
                op_queue_.splice(std::move(local_ops));
                if (!infinite) {
                    const auto op = op_queue_.try_dequeue();
                    if (op) op->finish();
                    return op != nullptr;
                }
                continue;
            }

            // Real I/O completion: retrieve the node from the extended OVERLAPPED.
            auto* ext = reinterpret_cast<iocp_awaitable*>(ov);
            iocp_node* inode = ext->node;
            // Set next_ to nullptr so the node can be safely re-enqueued.
            static_cast<node*>(inode)->next_ = nullptr;
            inode->complete(bytes, err); // sets result into the operation
            local_ops.unsynchronized_enqueue(*static_cast<node*>(inode));

            op_queue_.splice(std::move(local_ops));

            if (!infinite) {
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
            ZeroMemory(&awaitable.ov, sizeof(awaitable.ov));
            const BOOL ok = ::ReadFile(
                handle,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                nullptr,
                &awaitable.ov
            );
            if (!ok) {
                const DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                if (err == ERROR_HANDLE_EOF) {
                    result.set_value(0);
                    immediately_post();
                }
                else {
                    result.set_error(to_error_code(err));
                    return false;
                }
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_read_some_t>::do_cancel() -> void {
            ::CancelIoEx(handle, &awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_read_some_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) {
                result.set_stopped();
            }
            else if (error == ERROR_HANDLE_EOF or (error == 0 and bytes == 0 and !buffer.empty())) {
                result.set_value(0); // EOF signalled as 0 bytes
            }
            else if (error != 0) {
                result.set_error(to_error_code(error));
            }
            else {
                result.set_value(static_cast<std::size_t>(bytes));
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
            ZeroMemory(&awaitable.ov, sizeof(awaitable.ov));
            const BOOL ok = ::WriteFile(
                handle,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                nullptr,
                &awaitable.ov
            );
            if (!ok) {
                const DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                result.set_error(to_error_code(err));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_write_some_t>::do_cancel() -> void {
            ::CancelIoEx(handle, &awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_write_some_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) { result.set_stopped(); }
            else if (error != 0) { result.set_error(to_error_code(error)); }
            else { result.set_value(static_cast<std::size_t>(bytes)); }
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
            ZeroMemory(&awaitable.ov, sizeof(awaitable.ov));
            awaitable.ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
            awaitable.ov.OffsetHigh = static_cast<DWORD>(offset >> 32u);
            const BOOL ok = ::ReadFile(
                handle,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                nullptr,
                &awaitable.ov
            );
            if (!ok) {
                const DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                if (err == ERROR_HANDLE_EOF) {
                    result.set_value(0);
                    immediately_post();
                }
                else {
                    result.set_error(to_error_code(err));
                    return false;
                }
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_read_some_at_t>::do_cancel() -> void {
            ::CancelIoEx(handle, &awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_read_some_at_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) {
                result.set_stopped();
            }
            else if (error == ERROR_HANDLE_EOF or (error == 0 and bytes == 0 and !buffer.empty())) {
                result.set_value(0);
            }
            else if (error != 0) {
                result.set_error(to_error_code(error));
            }
            else {
                result.set_value(static_cast<std::size_t>(bytes));
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
            ZeroMemory(&awaitable.ov, sizeof(awaitable.ov));
            awaitable.ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
            awaitable.ov.OffsetHigh = static_cast<DWORD>(offset >> 32u);
            const BOOL ok = ::WriteFile(
                handle,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                nullptr,
                &awaitable.ov
            );
            if (!ok) {
                const DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) return true;
                result.set_error(to_error_code(err));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_write_some_at_t>::do_cancel() -> void {
            ::CancelIoEx(handle, &awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_write_some_at_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) { result.set_stopped(); }
            else if (error != 0) { result.set_error(to_error_code(error)); }
            else { result.set_value(static_cast<std::size_t>(bytes)); }
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
            ZeroMemory(&awaitable.ov, sizeof(awaitable.ov));
            WSABUF wsabuf{
                static_cast<ULONG>(buffer.size()),
                reinterpret_cast<CHAR*>(buffer.data())
            };
            DWORD flags = 0;
            const int rc = ::WSARecv(
                to_socket(handle),
                &wsabuf,
                1,
                nullptr,
                &flags,
                &awaitable.ov,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                result.set_error(to_error_code(static_cast<DWORD>(err)));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_receive_t>::do_cancel() -> void {
            ::CancelIoEx(handle, &awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_receive_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) {
                result.set_stopped();
            }
            else if (error != 0) {
                result.set_error(to_error_code(error));
            }
            else if (bytes == 0 and !buffer.empty()) {
                result.set_error(make_error_code(coio::error::eof));
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
            ZeroMemory(&awaitable.ov, sizeof(awaitable.ov));
            WSABUF wsabuf{
                static_cast<ULONG>(buffer.size()),
                const_cast<CHAR*>(reinterpret_cast<const CHAR*>(buffer.data()))
            };
            const int rc = ::WSASend(
                to_socket(handle),
                &wsabuf,
                1,
                nullptr,
                0,
                &awaitable.ov,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                result.set_error(to_error_code(static_cast<DWORD>(err)));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_send_t>::do_cancel() -> void {
            ::CancelIoEx(handle, &awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_send_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) { result.set_stopped(); }
            else if (error != 0) { result.set_error(to_error_code(error)); }
            else { result.set_value(static_cast<std::size_t>(bytes)); }
        }

        /// async_receive_from
        template<>
        auto iocp_state_base_for<async_receive_from_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            ZeroMemory(&this->awaitable.ov, sizeof(this->awaitable.ov));
            ZeroMemory(&this->from_addr, sizeof(this->from_addr));
            this->from_len = sizeof(SOCKADDR_STORAGE);
            this->wsabuf = {
                static_cast<ULONG>(this->buffer.size()),
                reinterpret_cast<CHAR*>(this->buffer.data())
            };
            DWORD flags = 0;
            const int rc = ::WSARecvFrom(
                to_socket(this->handle),
                &this->wsabuf,
                1,
                nullptr,
                &flags,
                reinterpret_cast<SOCKADDR*>(&this->from_addr),
                &this->from_len,
                &this->awaitable.ov,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                this->result.set_error(to_error_code(static_cast<DWORD>(err)));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_receive_from_t>::do_cancel() -> void {
            ::CancelIoEx(this->handle, &this->awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_receive_from_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) {
                this->result.set_stopped();
            }
            else if (error != 0) {
                this->result.set_error(to_error_code(error));
            }
            else {
                this->peer = sockaddr_storage_to_endpoint(this->from_addr);
                this->result.set_value(static_cast<std::size_t>(bytes));
            }
        }

        /// async_send_to
        template<>
        auto iocp_state_base_for<async_send_to_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            ZeroMemory(&this->awaitable.ov, sizeof(this->awaitable.ov));
            this->wsabuf = {
                static_cast<ULONG>(this->buffer.size()),
                const_cast<CHAR*>(reinterpret_cast<const CHAR*>(this->buffer.data()))
            };
            auto sa_variant = endpoint_to_sockaddr_in(this->peer);
            auto [psa, salen] = to_sockaddr(sa_variant);
            // Copy to stable storage inside this (native_iocp_sexpr<async_send_to_t>::type).
            std::memcpy(&this->to_addr, psa, static_cast<std::size_t>(salen));
            const int rc = ::WSASendTo(
                to_socket(this->handle),
                &this->wsabuf,
                1,
                nullptr,
                0,
                reinterpret_cast<SOCKADDR*>(&this->to_addr),
                salen,
                &this->awaitable.ov,
                nullptr
            );
            if (rc == SOCKET_ERROR) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                this->result.set_error(to_error_code(static_cast<DWORD>(err)));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_send_to_t>::do_cancel() -> void {
            ::CancelIoEx(this->handle, &this->awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_send_to_t>::complete(DWORD bytes, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) { result.set_stopped(); }
            else if (error != 0) { result.set_error(to_error_code(error)); }
            else { result.set_value(static_cast<std::size_t>(bytes)); }
        }

        /// async_accept
        template<>
        auto iocp_state_base_for<async_accept_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            // Determine family from the listening socket.
            SOCKADDR_STORAGE ls_addr{};
            int ls_len = sizeof(ls_addr);
            int family = AF_INET;
            if (::getsockname(
                    to_socket(this->handle),
                    reinterpret_cast<SOCKADDR*>(&ls_addr),
                    &ls_len
                ) == 0) {
                family = ls_addr.ss_family;
            }

            // Create the accept socket (must match listener's family/type/proto).
            this->accept_sock = ::WSASocketW(
                family,
                SOCK_STREAM,
                IPPROTO_TCP,
                nullptr,
                0,
                WSA_FLAG_OVERLAPPED
            );
            if (this->accept_sock == INVALID_SOCKET) {
                this->result.set_error(to_error_code(static_cast<DWORD>(::WSAGetLastError())));
                return false;
            }

            ZeroMemory(&this->awaitable.ov, sizeof(this->awaitable.ov));
            DWORD bytes_received = 0;
            const auto acceptex = get_acceptex_fn();
            if (!acceptex) {
                ::closesocket(this->accept_sock);
                this->accept_sock = INVALID_SOCKET;
                this->result.set_error(to_error_code(ERROR_NOT_SUPPORTED));
                return false;
            }
            const BOOL ok = ::AcceptEx(
                to_socket(this->handle),
                this->accept_sock,
                this->addr_buf,
                0u,
                // no data to receive
                sizeof(SOCKADDR_STORAGE) + 16u,
                // local addr slot
                sizeof(SOCKADDR_STORAGE) + 16u,
                // remote addr slot
                &bytes_received,
                &this->awaitable.ov
            );
            if (!ok) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                ::closesocket(this->accept_sock);
                this->accept_sock = INVALID_SOCKET;
                this->result.set_error(to_error_code(static_cast<DWORD>(err)));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_accept_t>::do_cancel() -> void {
            ::CancelIoEx(this->handle, &this->awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_accept_t>::complete(DWORD /*bytes*/, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED or error != 0) {
                if (this->accept_sock != INVALID_SOCKET) {
                    ::closesocket(this->accept_sock);
                    this->accept_sock = INVALID_SOCKET;
                }
                if (error == ERROR_OPERATION_ABORTED) this->result.set_stopped();
                else this->result.set_error(to_error_code(error));
                return;
            }
            // Make the accepted socket fully functional.
            const SOCKET ls = to_socket(this->handle);
            ::setsockopt(
                this->accept_sock,
                SOL_SOCKET,
                SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<const char*>(&ls),
                sizeof(ls)
            );
            this->result.set_value(from_socket(this->accept_sock));
        }

        /// async_connect
        template<>
        auto iocp_state_base_for<async_connect_t>::do_start() noexcept -> bool {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                result.set_error(std::make_error_code(std::errc::bad_file_descriptor));
                return false;
            }
            auto sa_variant = endpoint_to_sockaddr_in(this->peer);
            auto [psa, salen] = to_sockaddr(sa_variant);
            std::memcpy(&this->peer_sa, psa, static_cast<std::size_t>(salen));
            this->peer_sa_len = salen;

            const SOCKET sock = to_socket(this->handle);

            ZeroMemory(&this->awaitable.ov, sizeof(this->awaitable.ov));
            DWORD bytes_sent = 0;

            const auto connectex = get_connectex_fn();
            if (!connectex) {
                this->result.set_error(to_error_code(ERROR_NOT_SUPPORTED));
                return false;
            }
            const BOOL ok = connectex(
                sock,
                reinterpret_cast<SOCKADDR*>(&this->peer_sa),
                this->peer_sa_len,
                nullptr,
                0,
                &bytes_sent,
                &this->awaitable.ov
            );
            if (!ok) {
                const int err = ::WSAGetLastError();
                if (err == WSA_IO_PENDING) return true;
                this->result.set_error(to_error_code(static_cast<DWORD>(err)));
                return false;
            }
            return true;
        }

        template<>
        auto iocp_state_base_for<async_connect_t>::do_cancel() -> void {
            ::CancelIoEx(this->handle, &this->awaitable.ov);
        }

        template<>
        auto iocp_state_base_for<async_connect_t>::complete(DWORD /*bytes*/, DWORD error) noexcept -> void {
            if (error == ERROR_OPERATION_ABORTED) {
                this->result.set_stopped();
                return;
            }
            if (error != 0) {
                this->result.set_error(to_error_code(error));
                return;
            }
            // Update the socket context so that standard socket functions work.
            ::setsockopt(
                to_socket(this->handle),
                SOL_SOCKET,
                SO_UPDATE_CONNECT_CONTEXT,
                nullptr,
                0
            );
            this->result.set_value();
        }
    }
}
#endif
