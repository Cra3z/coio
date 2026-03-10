#include <coio/asyncio/file.h>
#include <coio/utils/scope_exit.h>
#include "../common.h"

namespace coio::detail {
    namespace {
        auto to_native_openmode(open_mode mode, bool random_access) noexcept -> std::pair<::DWORD, ::DWORD> {
            ::DWORD access = 0;
            if (bool(mode & open_mode::read_only))  access |= GENERIC_READ;
            if (bool(mode & open_mode::write_only)) access |= GENERIC_WRITE;
            if (bool(mode & open_mode::read_write)) access |= GENERIC_READ | GENERIC_WRITE;
            if (bool(mode & open_mode::append))     access |= FILE_APPEND_DATA;

            ::DWORD flags = FILE_FLAG_OVERLAPPED;
            if (random_access) flags |= FILE_FLAG_RANDOM_ACCESS;
            if (bool(mode & open_mode::sync_all_on_write)) flags |= FILE_FLAG_WRITE_THROUGH;
            return {access, flags};
        }

        auto to_creation_disposition(open_mode mode) noexcept -> ::DWORD {
            const bool create    = bool(mode & open_mode::create);
            const bool exclusive = bool(mode & open_mode::exclusive);
            const bool truncate  = bool(mode & open_mode::truncate);

            if (exclusive) return CREATE_NEW;
            if (truncate and create) return CREATE_ALWAYS;
            if (truncate) return TRUNCATE_EXISTING;
            if (create) return OPEN_ALWAYS;
            return OPEN_EXISTING;
        }

        auto sync_file_read(::HANDLE handle, std::span<std::byte> buffer, std::size_t offset, const char* msg) -> std::size_t {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), msg};
            }
            if (buffer.empty()) [[unlikely]] return 0;

            const ::HANDLE reset_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (reset_event == nullptr) {
                throw std::system_error{to_error_code(::GetLastError()), msg};
            }
            scope_exit _{[reset_event] {
                ::CloseHandle(reset_event);
            }};

            ::OVERLAPPED overlapped{
                .Offset = static_cast<::DWORD>(offset & 0xff'ff'ff'ffu),
                .OffsetHigh = static_cast<::DWORD>(offset >> 32u),
                .hEvent = reset_event // for GetOverlappedResult
            };
            ::DWORD n = 0;

            do {
                if (not ::ReadFile(handle, buffer.data(), std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu), &n, &overlapped)) {
                    ::DWORD err = ::GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        if (not ::GetOverlappedResult(handle, &overlapped, &n, TRUE)) {
                            err = ::GetLastError();
                            if (err == ERROR_HANDLE_EOF) throw std::system_error{coio::error::eof, msg};
                            throw std::system_error{static_cast<int>(err), std::system_category(), msg};
                        }
                    }
                    else if (err == ERROR_OPERATION_ABORTED) continue;
                    else if (err == ERROR_HANDLE_EOF) throw std::system_error{coio::error::eof, msg};
                    else throw std::system_error{static_cast<int>(err), std::system_category(), msg};
                }
                break;
            }
            while (true);

            if (not buffer.empty() and n == 0) {
                throw std::system_error{coio::error::eof, msg};
            }
            return n;
        }

        auto sync_file_write(::HANDLE handle, std::span<const std::byte> buffer, std::size_t offset, const char* msg) -> std::size_t {
            if (handle == INVALID_HANDLE_VALUE) {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), msg};
            }
            if (buffer.empty()) [[unlikely]] return 0;

            const ::HANDLE reset_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (reset_event == nullptr) {
                throw std::system_error{to_error_code(::GetLastError()), msg};
            }
            scope_exit _{[reset_event] {
                ::CloseHandle(reset_event);
            }};

            ::OVERLAPPED overlapped{
                .Offset = static_cast<::DWORD>(offset & 0xff'ff'ff'ffu),
                .OffsetHigh = static_cast<::DWORD>(offset >> 32u),
                .hEvent = reset_event // for GetOverlappedResult
            };
            ::DWORD n = 0;

            do {
                if (not ::WriteFile(handle, buffer.data(), std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu), &n, &overlapped)) {
                    ::DWORD err = ::GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        if (not ::GetOverlappedResult(handle, &overlapped, &n, TRUE)) {
                            err = ::GetLastError();
                            throw std::system_error{static_cast<int>(err), std::system_category(), msg};
                        }
                    }
                    else if (err == ERROR_OPERATION_ABORTED) continue;
                    else throw std::system_error{static_cast<int>(err), std::system_category(), msg};
                }
                break;
            }
            while (true);
            return n;
        }
    }

    auto open_file(zstring_view path, open_mode mode, bool random_access) -> file_native_handle_type {
        auto [access, flags] = to_native_openmode(mode, random_access);
        const ::DWORD disposition = to_creation_disposition(mode);
        const auto handle = ::CreateFileA(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, disposition, flags, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            throw std::system_error(to_error_code(::GetLastError()), "open");
        }
        return handle;
    }

    auto file_read(file_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t {
        return sync_file_read(handle, buffer, 0, "read_some");
    }

    auto file_write(file_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
        return sync_file_write(handle, buffer, 0, "write_some");
    }

    auto file_read_at(file_native_handle_type handle, std::size_t offset, std::span<std::byte> buffer) -> std::size_t {
        return sync_file_read(handle, buffer, offset, "read_some_at");
    }

    auto file_write_at(file_native_handle_type handle, std::size_t offset, std::span<const std::byte> buffer) -> std::size_t {
        return sync_file_write(handle, buffer, offset, "write_some_at");
    }

    auto close_file(file_native_handle_type handle) -> void {
        if (handle == invalid_file_handle) return;
        if (not ::CloseHandle(handle)) {
            throw std::system_error(to_error_code(::GetLastError()), "close");
        }
    }

    auto file_seek(file_native_handle_type handle, std::size_t offset, seek_whence whence) -> std::size_t {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), "seek"};
        }

        ::DWORD move_method;
        switch (whence) {
        case seek_whence::seek_set: move_method = FILE_BEGIN;   break;
        case seek_whence::seek_cur: move_method = FILE_CURRENT; break;
        case seek_whence::seek_end: move_method = FILE_END;     break;
        default: unreachable();
        }

        ::LARGE_INTEGER dist{}, result{};
        dist.QuadPart = static_cast<::LONGLONG>(offset);
        if (not ::SetFilePointerEx(handle, dist, &result, move_method)) {
            throw std::system_error(to_error_code(::GetLastError()), "seek");
        }
        return static_cast<std::size_t>(result.QuadPart);
    }

    auto file_size(file_native_handle_type handle) -> std::size_t {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), "size"};
        }
        ::LARGE_INTEGER sz{};
        if (not ::GetFileSizeEx(handle, &sz)) {
            throw std::system_error(to_error_code(::GetLastError()), "size");
        }
        return static_cast<std::size_t>(sz.QuadPart);
    }

    auto file_resize(file_native_handle_type handle, std::size_t new_size) -> void {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), "resize"};
        }
        ::LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<::LONGLONG>(new_size);
        if (not ::SetFilePointerEx(handle, pos, nullptr, FILE_BEGIN)) {
            throw std::system_error(to_error_code(::GetLastError()), "resize");
        }
        if (not ::SetEndOfFile(handle)) {
            throw std::system_error(to_error_code(::GetLastError()), "resize");
        }
    }

    auto file_sync_all(file_native_handle_type handle) -> void {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), "sync_all"};
        }
        if (not ::FlushFileBuffers(handle)) {
            throw std::system_error(to_error_code(::GetLastError()), "sync_all");
        }
    }

    auto file_sync_data(file_native_handle_type handle) -> void {
        file_sync_all(handle);
    }
}