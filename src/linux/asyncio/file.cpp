// NOLINTBEGIN(*-narrowing-conversions)
#include <fcntl.h>
#include <sys/stat.h>
#include <coio/asyncio/file.h>
#include "../common.h"

namespace coio::detail {
    namespace {
        COIO_ALWAYS_INLINE auto to_native_openmode(open_mode mode) noexcept -> int {
            int result = 0;
            if (bool(mode & open_mode::read_only)) {
                result |= O_RDONLY;
            }
            if (bool(mode & open_mode::write_only)) {
                result |= O_WRONLY;
            }
            if (bool(mode & open_mode::read_write)) {
                result |= O_RDWR;
            }
            if (bool(mode & open_mode::append)) {
                result |= O_APPEND;
            }
            if (bool(mode & open_mode::create)) {
                result |= O_CREAT;
            }
            if (bool(mode & open_mode::exclusive)) {
                result |= O_EXCL;
            }
            if (bool(mode & open_mode::truncate)) {
                result |= O_TRUNC;
            }
            if (bool(mode & open_mode::sync_all_on_write)) {
                result |= O_SYNC;
            }
            return result;
        }
    }

    auto open_file(zstring_view path, open_mode mode, bool random_access) -> file_native_handle_type {
        const auto fd = ::open(path.c_str(), to_native_openmode(mode), 0777);
        throw_last_error(fd, "open");
        if (random_access) ::posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
        return fd;
    }

    auto file_read(file_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t {
        const auto n = ::read(handle, buffer.data(), buffer.size());
        throw_last_error(n, "read_some");
        if (not buffer.empty() and n == 0) throw std::system_error{coio::error::eof, "read_some"};
        return n;
    }

    auto file_write(file_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t {
        const auto n = ::write(handle, buffer.data(), buffer.size());
        throw_last_error(n, "write_some");
        return n;
    }

    auto file_read_at(file_native_handle_type handle, std::size_t offset, std::span<std::byte> buffer) -> std::size_t {
        if (offset > std::numeric_limits<::off_t>::max()) [[unlikely]] {
            throw std::system_error{std::make_error_code(std::errc::value_too_large), "read_some_at"};
        }
        const auto n = ::pread(handle, buffer.data(), buffer.size(), offset);
        throw_last_error(n, "read_some_at");
        if (not buffer.empty() and n == 0) throw std::system_error{coio::error::eof, "read_some_at"};
        return n;
    }

    auto file_write_at(file_native_handle_type handle, std::size_t offset, std::span<const std::byte> buffer) -> std::size_t {
        if (offset > std::numeric_limits<::off_t>::max()) [[unlikely]] {
            throw std::system_error{std::make_error_code(std::errc::value_too_large), "write_some_at"};
        }
        const auto n = ::pwrite(handle, buffer.data(), buffer.size(), offset);
        throw_last_error(n, "write_some_at");
        return n;
    }

    auto close_file(file_native_handle_type handle) -> void {
        if (handle == -1) return;
        throw_last_error(::close(handle), "close");
    }

    auto file_seek(file_native_handle_type handle, std::size_t offset, seek_whence whence) -> std::size_t {
        if (offset > std::numeric_limits<::off_t>::max()) [[unlikely]] {
            throw std::system_error{std::make_error_code(std::errc::value_too_large), "seek"};
        }
        int native_whence;
        switch (whence) {
        case seek_whence::seek_set: {
            native_whence = SEEK_SET;
            break;
        }
        case seek_whence::seek_cur: {
            native_whence = SEEK_CUR;
            break;
        }
        case seek_whence::seek_end: {
            native_whence = SEEK_END;
            break;
        }
        default: unreachable();
        }
        const auto result = ::lseek(handle, offset, native_whence);
        throw_last_error(result, "seek");
        return result;
    }

    auto file_size(file_native_handle_type handle) -> std::size_t {
        struct ::stat st; // NOLINT(cppcoreguidelines-pro-type-member-init)
        throw_last_error(::fstat(handle, &st), "size");
        return st.st_size;
    }

    auto file_resize(file_native_handle_type handle, std::size_t new_size) -> void {
        if (new_size > std::numeric_limits<::off_t>::max()) [[unlikely]] {
            throw std::system_error{std::make_error_code(std::errc::value_too_large), "resize"};
        }
        throw_last_error(::ftruncate(handle, new_size), "resize");
    }

    auto file_sync_all(file_native_handle_type handle) -> void {
        throw_last_error(::fsync(handle), "sync_all");
    }

    auto file_sync_data(file_native_handle_type handle) -> void {
        throw_last_error(::fdatasync(handle), "sync_data");
    }
}

// NOLINTEND(*-narrowing-conversions)