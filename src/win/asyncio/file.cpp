#include <bit>
#include <coio/asyncio/file.h>
#include <coio/utils/scope_exit.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep
#include "../common.h"

namespace coio::detail {
    namespace {
        auto to_native_openmode(open_mode mode, bool random_access) noexcept -> std::pair<::DWORD, ::DWORD> {
            ::DWORD access = 0;
            if (bool(mode & open_mode::read_only))  access |= GENERIC_READ;
            if (bool(mode & open_mode::write_only)) access |= GENERIC_WRITE;
            if (bool(mode & open_mode::read_write)) access |= GENERIC_READ | GENERIC_WRITE;

            ::DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
            if (random_access) flags |= FILE_FLAG_RANDOM_ACCESS;
            else flags |= FILE_FLAG_SEQUENTIAL_SCAN;
            if (bool(mode & open_mode::sync_all_on_write)) flags |= FILE_FLAG_WRITE_THROUGH;
            return {access, flags};
        }

        auto to_creation_disposition(open_mode mode) noexcept -> ::DWORD {
            if (bool(mode & open_mode::create)) {
                if (bool(mode & open_mode::exclusive)) return CREATE_NEW;
                return OPEN_ALWAYS;
            }

            if (bool(mode & open_mode::truncate)) return TRUNCATE_EXISTING;
            return OPEN_EXISTING;
        }

        auto sync_file_read(::HANDLE handle, std::span<std::byte> buffer, std::size_t offset, const char* msg) -> std::size_t {
            if (handle == INVALID_HANDLE_VALUE) [[unlikely]] {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), msg};
            }
            if (buffer.empty()) [[unlikely]] return 0;

            ::HANDLE reset_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (reset_event == nullptr) {
                throw std::system_error{to_error_code(::GetLastError()), msg};
            }
            scope_exit _{[reset_event] {
                ::CloseHandle(reset_event);
            }};

            ::OVERLAPPED overlapped{
                .Offset = static_cast<::DWORD>(offset & 0xff'ff'ff'ffu),
                .OffsetHigh = static_cast<::DWORD>(offset >> 32u),
                .hEvent = std::bit_cast<::HANDLE>(std::bit_cast<::ULONG_PTR>(reset_event) | 1u) // for GetOverlappedResult
            };
            ::DWORD n = 0;

            if (not ::ReadFile(handle, buffer.data(), static_cast<::DWORD>(std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu)), &n, &overlapped)) {
                ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) {
                    err = ::GetOverlappedResult(handle, &overlapped, &n, TRUE) ? ERROR_SUCCESS : ::GetLastError();
                }
                // a message-mode pipe truncates the message to the buffer and reports it this way;
                // `n` still counts what was transferred, and read_some may return short anyway
                if (err == ERROR_MORE_DATA) err = ERROR_SUCCESS;
                if (err != ERROR_SUCCESS) {
                    // cancel() and close() reach this operation through CancelIoEx, so an abort almost
                    // always surfaces here rather than from ReadFile itself
                    if (err == ERROR_OPERATION_ABORTED) {
                        throw std::system_error{std::make_error_code(std::errc::operation_canceled), msg};
                    }
                    // ERROR_BROKEN_PIPE is how a pipe reports its writer closing: map it to EOF like POSIX read() == 0
                    if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE) throw std::system_error{coio::error::eof, msg};
                    throw std::system_error{static_cast<int>(err), std::system_category(), msg};
                }
            }

            if (n == 0) {
                throw std::system_error{coio::error::eof, msg};
            }
            return n;
        }

        auto sync_file_write(::HANDLE handle, std::span<const std::byte> buffer, std::size_t offset, const char* msg) -> std::size_t {
            if (handle == INVALID_HANDLE_VALUE) {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), msg};
            }
            if (buffer.empty()) [[unlikely]] return 0;

            ::HANDLE reset_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (reset_event == nullptr) {
                throw std::system_error{to_error_code(::GetLastError()), msg};
            }
            scope_exit _{[reset_event] {
                ::CloseHandle(reset_event);
            }};

            ::OVERLAPPED overlapped{
                .Offset = static_cast<::DWORD>(offset & 0xff'ff'ff'ffu),
                .OffsetHigh = static_cast<::DWORD>(offset >> 32u),
                .hEvent = std::bit_cast<::HANDLE>(std::bit_cast<::ULONG_PTR>(reset_event) | 1u) // for GetOverlappedResult
            };
            ::DWORD n = 0;

            if (not ::WriteFile(handle, buffer.data(), static_cast<::DWORD>(std::min<std::size_t>(buffer.size(), 0xff'ff'ff'ffu)), &n, &overlapped)) {
                ::DWORD err = ::GetLastError();
                if (err == ERROR_IO_PENDING) {
                    err = ::GetOverlappedResult(handle, &overlapped, &n, TRUE) ? ERROR_SUCCESS : ::GetLastError();
                }
                if (err != ERROR_SUCCESS) {
                    if (err == ERROR_OPERATION_ABORTED) {
                        throw std::system_error{std::make_error_code(std::errc::operation_canceled), msg};
                    }
                    throw std::system_error{static_cast<int>(err), std::system_category(), msg};
                }
            }
            return n;
        }
    }

    auto open_file(zstring_view path, open_mode mode, bool random_access) -> file_native_handle_type {
        auto [access, flags] = to_native_openmode(mode, random_access);
        const ::DWORD disposition = to_creation_disposition(mode);
        const auto handle = ::CreateFileA(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition, flags, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            throw std::system_error{to_error_code(::GetLastError()), "open"};
        }

        if (disposition == OPEN_ALWAYS) {
            if (bool(mode & open_mode::truncate)) {
                if (not ::SetEndOfFile(handle)) {
                    const ::DWORD err = ::GetLastError();
                    ::CloseHandle(handle);
                    throw std::system_error{to_error_code(err), "open"};
                }
            }
        }

        if (disposition == OPEN_ALWAYS or disposition == OPEN_EXISTING) {
            if (bool(mode & open_mode::append)) {
                ::LARGE_INTEGER distance_to_move{.QuadPart = 0};
                ::LARGE_INTEGER new_file_pointer{};
                if (not ::SetFilePointerEx(handle, distance_to_move, &new_file_pointer, FILE_END)) {
                    const ::DWORD err = ::GetLastError();
                    ::CloseHandle(handle);
                    throw std::system_error{to_error_code(err), "open"};
                }
            }
        }

        return handle;
    }

    auto file_read_at(file_native_handle_type handle, std::size_t offset, std::span<std::byte> buffer) -> std::size_t {
        return sync_file_read(handle, buffer, offset, "read_some_at");
    }

    auto file_write_at(file_native_handle_type handle, std::size_t offset, std::span<const std::byte> buffer) -> std::size_t {
        return sync_file_write(handle, buffer, offset, "write_some_at");
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

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
