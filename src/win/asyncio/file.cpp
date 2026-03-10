// NOLINTBEGIN(*-narrowing-conversions)
#include <coio/asyncio/file.h>
#include "../common.h"

namespace coio::detail {
    namespace {
        auto to_native_openmode(open_mode mode, bool random_access) noexcept
            -> std::pair<DWORD /*desired_access*/, DWORD /*flags_and_attrs*/>
        {
            DWORD access = 0;
            if (bool(mode & open_mode::read_only))  access |= GENERIC_READ;
            if (bool(mode & open_mode::write_only)) access |= GENERIC_WRITE;
            if (bool(mode & open_mode::read_write)) access |= GENERIC_READ | GENERIC_WRITE;
            if (bool(mode & open_mode::append))     access |= FILE_APPEND_DATA;

            DWORD flags = FILE_FLAG_OVERLAPPED;
            if (random_access)                       flags |= FILE_FLAG_RANDOM_ACCESS;
            if (bool(mode & open_mode::sync_all_on_write)) flags |= FILE_FLAG_WRITE_THROUGH;
            return {access, flags};
        }

        auto to_creation_disposition(open_mode mode) noexcept -> DWORD {
            const bool create    = bool(mode & open_mode::create);
            const bool exclusive = bool(mode & open_mode::exclusive);
            const bool truncate  = bool(mode & open_mode::truncate);

            if (exclusive) return CREATE_NEW;
            if (truncate && create) return CREATE_ALWAYS;
            if (truncate) return TRUNCATE_EXISTING;
            if (create)   return OPEN_ALWAYS;
            return OPEN_EXISTING;
        }
    }

    auto open_file(zstring_view path, open_mode mode, bool random_access)
        -> file_native_handle_type
    {
        auto [access, flags] = to_native_openmode(mode, random_access);
        const DWORD disposition = to_creation_disposition(mode);

        const HANDLE h = ::CreateFileA(path.c_str(), access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, disposition, flags, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            throw std::system_error(to_error_code(::GetLastError()), "open");
        }
        return h;
    }

    auto file_read(file_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
        }
        DWORD n = 0;
        const BOOL ok = ::ReadFile(handle, buffer.data(),
            static_cast<DWORD>(buffer.size()), &n, nullptr);
        if (!ok) throw std::system_error(to_error_code(::GetLastError()), "read_some");
        if (!buffer.empty() && n == 0)
            throw std::system_error{coio::error::eof, "read_some"};
        return static_cast<std::size_t>(n);
    }

    auto file_write(file_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
        }
        DWORD n = 0;
        const BOOL ok = ::WriteFile(handle, buffer.data(),
            static_cast<DWORD>(buffer.size()), &n, nullptr);
        if (!ok) throw std::system_error(to_error_code(::GetLastError()), "write_some");
        return static_cast<std::size_t>(n);
    }

    auto file_read_at(file_native_handle_type handle, std::size_t offset,
                      std::span<std::byte> buffer) -> std::size_t
    {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
        }
        OVERLAPPED ov{};
        ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32u);
        DWORD n = 0;
        const BOOL ok = ::ReadFile(handle, buffer.data(),
            static_cast<DWORD>(buffer.size()), &n, &ov);
        if (!ok) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_HANDLE_EOF) {
                if (!buffer.empty())
                    throw std::system_error{coio::error::eof, "read_some_at"};
                return 0;
            }
            throw std::system_error(static_cast<int>(err),
                std::system_category(), "read_some_at");
        }
        if (!buffer.empty() && n == 0)
            throw std::system_error{coio::error::eof, "read_some_at"};
        return static_cast<std::size_t>(n);
    }

    auto file_write_at(file_native_handle_type handle, std::size_t offset,
                       std::span<const std::byte> buffer) -> std::size_t
    {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
        }
        OVERLAPPED ov{};
        ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32u);
        DWORD n = 0;
        const BOOL ok = ::WriteFile(handle, buffer.data(),
            static_cast<DWORD>(buffer.size()), &n, &ov);
        if (!ok) throw std::system_error(to_error_code(::GetLastError()), "write_some_at");
        return static_cast<std::size_t>(n);
    }

    auto close_file(file_native_handle_type handle) -> void {
        if (handle == invalid_file_handle) return;
        if (!::CloseHandle(handle)) {
            throw std::system_error(to_error_code(::GetLastError()), "close");
        }
    }

    auto file_seek(file_native_handle_type handle,
                   std::size_t offset, seek_whence whence) -> std::size_t
    {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
        }
        DWORD move_method;
        switch (whence) {
        case seek_whence::seek_set: move_method = FILE_BEGIN;   break;
        case seek_whence::seek_cur: move_method = FILE_CURRENT; break;
        case seek_whence::seek_end: move_method = FILE_END;     break;
        default: unreachable();
        }
        LARGE_INTEGER dist{}, result{};
        dist.QuadPart = static_cast<LONGLONG>(offset);
        if (!::SetFilePointerEx(handle, dist, &result, move_method)) {
            throw std::system_error(to_error_code(::GetLastError()), "seek");
        }
        return static_cast<std::size_t>(result.QuadPart);
    }

    auto file_size(file_native_handle_type handle) -> std::size_t {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
        }
        LARGE_INTEGER sz{};
        if (!::GetFileSizeEx(handle, &sz)) {
            throw std::system_error(to_error_code(::GetLastError()), "size");
        }
        return static_cast<std::size_t>(sz.QuadPart);
    }

    auto file_resize(file_native_handle_type handle, std::size_t new_size) -> void {
        if (handle == invalid_file_handle) {
            throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor)};
        }
        // Seek to new size then set end-of-file.
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(new_size);
        if (!::SetFilePointerEx(handle, pos, nullptr, FILE_BEGIN)) {
            throw std::system_error(to_error_code(::GetLastError()), "resize");
        }
        if (!::SetEndOfFile(handle)) {
            throw std::system_error(to_error_code(::GetLastError()), "resize");
        }
    }

    auto file_sync_all(file_native_handle_type handle) -> void {
        if (handle == invalid_file_handle) return;
        if (!::FlushFileBuffers(handle)) {
            throw std::system_error(to_error_code(::GetLastError()), "sync_all");
        }
    }

    auto file_sync_data(file_native_handle_type handle) -> void {
        // Windows FlushFileBuffers flushes both data and metadata.
        file_sync_all(handle);
    }
}
// NOLINTEND(*-narrowing-conversions)