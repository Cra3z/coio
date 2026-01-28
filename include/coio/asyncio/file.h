#pragma once
#include <filesystem>
#include <utility>
#include "../core.h"
#include "../detail/async_result.h"
#include "../detail/error.h"
#include "../detail/io_descriptions.h"
#include "../utils/zstring_view.h"

namespace coio {
    namespace detail {
#if COIO_OS_LINUX
        using file_native_handle_type = int;

        inline constexpr file_native_handle_type invalid_file_handle = -1;
#elif COIO_OS_WINDOWS
        using file_native_handle_type = void*;

        inline constexpr file_native_handle_type invalid_file_handle = nullptr;
#endif
        /**
         * \brief File open mode flags.
         *
         * These flags can be combined using the bitwise OR operator (|) to specify
         * multiple file opening behaviors.
         */
        enum class open_mode {
            read_only = 1,           ///< Open file for reading only
            write_only = 2,          ///< Open file for writing only
            read_write = 4,          ///< Open file for both reading and writing
            append = 8,              ///< Append mode: writes occur at end of file
            create = 16,             ///< Create file if it doesn't exist
            exclusive = 32,          ///< Ensure creation of a new file (fails if file exists)
            truncate = 64,           ///< Truncate existing file to zero length
            sync_all_on_write = 128  ///< Synchronize all writes to disk immediately
        };

        COIO_ALWAYS_INLINE constexpr auto operator| (open_mode lhs, open_mode rhs) noexcept -> open_mode {
            return static_cast<open_mode>(int(lhs) | int(rhs));
        }

        COIO_ALWAYS_INLINE constexpr auto operator& (open_mode lhs, open_mode rhs) noexcept -> open_mode {
            return static_cast<open_mode>(int(lhs) & int(rhs));
        }

        /**
         * \brief File seek origin specification.
         *
         * Specifies the position from which to calculate the new file position
         * when performing a seek operation.
         */
        enum class seek_whence {
            seek_set = 0,  ///< Seek from beginning of file
            seek_cur = 1,  ///< Seek from current file position
            seek_end = 2   ///< Seek from end of file
        };

        /**
         * \brief Open a file with the specified mode.
         * \param path The path of the file to open.
         * \param mode The mode in which to open the file.
         * \param random_access Whether to optimize the file for random access.
         * \return The native file handle.
         * \throw std::system_error on failure.
         */
        auto open_file(zstring_view path, open_mode mode, bool random_access) -> file_native_handle_type;

        /**
         * \brief Read data from a file.
         * \param handle The native file handle.
         * \param buffer The buffer to receive the data.
         * \return The number of bytes read.
         * \throw std::system_error on failure.
         */
        auto file_read(file_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t;

        /**
         * \brief Write data to a file.
         * \param handle The native file handle.
         * \param buffer The data to write.
         * \return The number of bytes written.
         * \throw std::system_error on failure.
         */
        auto file_write(file_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t;

        /**
         * \brief Read data from a file at a specified offset.
         * \param handle The native file handle.
         * \param offset The offset at which to read.
         * \param buffer The buffer to receive the data.
         * \return The number of bytes read.
         * \throw std::system_error on failure.
         */
        auto file_read_at(file_native_handle_type handle, std::size_t offset, std::span<std::byte> buffer) -> std::size_t;

        /**
         * \brief Write data to a file at a specified offset.
         * \param handle The native file handle.
         * \param offset The offset at which to write.
         * \param buffer The data to write.
         * \return The number of bytes written.
         * \throw std::system_error on failure.
         */
        auto file_write_at(file_native_handle_type handle, std::size_t offset, std::span<const std::byte> buffer) -> std::size_t;

        /**
         * \brief Close a file.
         * \param handle The native file handle to close.
         * \throw std::system_error on failure.
         */
        auto close_file(file_native_handle_type handle) -> void;

        /**
         * \brief Seek to a position in a file.
         * \param handle The native file handle.
         * \param offset The offset to seek to.
         * \param whence The position from which to calculate the new position.
         * \return The new position from the beginning of the file.
         * \throw std::system_error on failure.
         */
        auto file_seek(file_native_handle_type handle, std::size_t offset, seek_whence whence) -> std::size_t;

        /**
         * \brief Get the size of a file.
         * \param handle The native file handle.
         * \return The size of the file in bytes.
         * \throw std::system_error on failure.
         */
        auto file_size(file_native_handle_type handle) -> std::size_t;

        /**
         * \brief Resize a file.
         * \param handle The native file handle.
         * \param new_size The new size of the file in bytes.
         * \throw std::system_error on failure.
         */
        auto file_resize(file_native_handle_type handle, std::size_t new_size) -> void;

        /**
         * \brief Synchronize file data and metadata with the storage device.
         * \param handle The native file handle.
         * \throw std::system_error on failure.
         */
        auto file_sync_all(file_native_handle_type handle) -> void;

        /**
         * \brief Synchronize file data with the storage device.
         * \param handle The native file handle.
         * \throw std::system_error on failure.
         */
        auto file_sync_data(file_native_handle_type handle) -> void;

        template<io_scheduler IoScheduler>
        class file_base {
        private:
            using implementation_type = decltype(std::declval<IoScheduler&>().make_io_object(std::declval<file_native_handle_type>()));

        public:
            using native_handle_type = file_native_handle_type;
            using scheduler_type = IoScheduler;

        public:
            explicit file_base(scheduler_type scheduler) noexcept : file_base(std::move(scheduler), invalid_file_handle) {}

            file_base(scheduler_type scheduler, native_handle_type handle) : impl_(scheduler.make_io_object(handle)) {}

            file_base(const file_base&) = delete;

            file_base(file_base&& other) = default;

            ~file_base() noexcept {
                close();
            }

            auto operator= (const file_base&) -> file_base& = delete;

            auto operator= (file_base&& other) -> file_base& = default;

            /**
             * \brief Get the io scheduler associated with the file.
             * \return The io scheduler.
             */
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler_type {
                return impl_.get_io_scheduler();
            }

            /**
             * \brief Close the file.
             *
             * Any asynchronous operations on the file will be cancelled immediately.
             * \throw std::system_error on failure.
             */
            COIO_ALWAYS_INLINE auto close() -> void {
                close_file(release());
            }

            /**
             * \brief Release ownership of the native file handle.
             *
             * This function causes all outstanding asynchronous operations to finish as soon as possible.
             * The file object releases ownership of the native handle, which can then be managed externally.
             * \return The native file handle.
             */
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto release() -> native_handle_type {
                return impl_.release();
            }

            /**
             * \brief Cancel all asynchronous operations associated with the file.
             *
             * Any asynchronous read or write operations will be cancelled immediately and
             * will complete with an operation_aborted error.
             */
            COIO_ALWAYS_INLINE auto cancel() -> void {
                impl_.cancel();
            }

            /**
             * \brief Get the native file handle.
             * \return The native file handle.
             */
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto native_handle() const noexcept -> file_native_handle_type {
                return impl_.native_handle();
            }

            /**
             * \brief Determine whether the file is open.
             * \return true if the file is open, false otherwise.
             */
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto is_open() const noexcept -> bool {
                return native_handle() != invalid_file_handle;
            }

            /**
             * \brief Determine whether the file is open.
             *
             * Same as is_open().
             * \return true if the file is open, false otherwise.
             */
            COIO_ALWAYS_INLINE explicit operator bool() const noexcept {
                return is_open();
            }

        protected:
            implementation_type impl_;
        };

        template<io_scheduler IoScheduler>
        class stream_file_base : public detail::file_base<IoScheduler> {
        private:
            using base = detail::file_base<IoScheduler>;

        public:
            using base::base;

            /**
             * \brief Read some data from the file.
             *
             * This function synchronously reads data from the file at the current file position.
             * The file position is advanced by the number of bytes read.
             * \param buffer A buffer to receive the data read from the file.
             * \return The number of bytes read.
             * \throw std::system_error on failure.
             */
            COIO_ALWAYS_INLINE auto read_some(std::span<std::byte> buffer) -> std::size_t {
                return detail::file_read(this->native_handle(), buffer);
            }

            /**
             * \brief asynchronously read some data.
             * \param buffer the buffer to read into.
             * \return an awaitable of `std::size_t`.
             */
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto async_read_some(std::span<std::byte> buffer) {
                return then(
                    this->get_io_scheduler().schedule_io(
                        this->impl_,
                        detail::async_read_some_t{buffer}
                    ),
                    [total = buffer.size()](std::size_t bytes_transferred) -> std::size_t {
                        if (bytes_transferred == 0 and total > 0) [[unlikely]] {
                            throw std::system_error{coio::error::eof, "async_read_some"};
                        }
                        return bytes_transferred;
                    }
                );
            }

            /**
             * \brief Write some data to the file.
             *
             * This function synchronously writes data to the file at the current file position.
             * The file position is advanced by the number of bytes written.
             * \param buffer The data to be written to the file.
             * \return The number of bytes written.
             * \throw std::system_error on failure.
             */
            COIO_ALWAYS_INLINE auto write_some(std::span<const std::byte> buffer) -> std::size_t {
                return detail::file_write(this->native_handle(), buffer);
            }

            /**
             * \brief asynchronously write some data.
             * \param buffer the buffer to write from.
             * \return an awaitable of `std::size_t`.
             */
            [[nodiscard]]
            COIO_ALWAYS_INLINE auto async_write_some(std::span<const std::byte> buffer) {
                return this->get_io_scheduler().schedule_io(this->impl_, detail::async_write_some_t{buffer});
            }
        };

        template<io_scheduler IoScheduler>
        class random_access_file_base : public detail::file_base<IoScheduler> {
        private:
            using base = detail::file_base<IoScheduler>;

        public:
            using base::base;

            /**
             * \brief Read some data from the file at a specified offset.
             *
             * This function synchronously reads data from the file at the specified offset.
             * The file position is not changed by this operation.
             * \param offset The offset at which the data will be read.
             * \param buffer A buffer to receive the data read from the file.
             * \return The number of bytes read.
             * \throw std::system_error on failure.
             */
            COIO_ALWAYS_INLINE auto read_some_at(std::size_t offset, std::span<std::byte> buffer) -> std::size_t {
                return detail::file_read_at(this->native_handle(), offset, buffer);
            }

            /**
             * \brief Asynchronously read some data from the file at a specified offset.
             *
             * This function asynchronously reads data from the file at the specified offset.
             * The file position is not changed by this operation.
             * \param offset The offset at which the data will be read.
             * \param buffer A buffer to receive the data read from the file.
             * \return An awaitable of `std::size_t` representing the number of bytes read.
             * \throw std::system_error if EOF is reached when buffer is not empty or on other failures.
             */
            COIO_ALWAYS_INLINE auto async_read_some_at(
                std::size_t offset,
                std::span<std::byte> buffer
            ) {
                return let_value(
                    this->get_io_scheduler().schedule_io(
                        this->impl_,
                        detail::async_read_some_at_t{offset, buffer}
                    ),
                    [total = buffer.size()](std::size_t bytes_transferred) noexcept -> detail::async_result<std::size_t, std::error_code> {
                        detail::async_result<std::size_t, std::error_code> result;
                        if (bytes_transferred == 0 and total > 0) [[unlikely]] {
                            result.set_error(error::eof);
                        }
                        else {
                            result.set_value(bytes_transferred);
                        }
                        return result;
                    }
                );
            }

            /**
             * \brief Write some data to the file at a specified offset.
             *
             * This function synchronously writes data to the file at the specified offset.
             * The file position is not changed by this operation.
             * \param offset The offset at which the data will be written.
             * \param buffer The data to be written to the file.
             * \return The number of bytes written.
             * \throw std::system_error on failure.
             */
            COIO_ALWAYS_INLINE auto write_some_at(std::size_t offset, std::span<const std::byte> buffer) -> std::size_t {
                return detail::file_write_at(this->native_handle(), offset, buffer);
            }

            /**
             * \brief Asynchronously write some data to the file at a specified offset.
             *
             * This function asynchronously writes data to the file at the specified offset.
             * The file position is not changed by this operation.
             * \param offset The offset at which the data will be written.
             * \param buffer The data to be written to the file.
             * \return An awaitable of `std::size_t` representing the number of bytes written.
             * \throw std::system_error on failure.
             */
            COIO_ALWAYS_INLINE auto async_write_some_at(
                std::size_t offset,
                std::span<const std::byte> buffer
            ) -> std::size_t {
                return this->get_io_scheduler().schedule_io(
                    this->impl_,
                    detail::async_write_some_at_t{offset, buffer}
                );
            }
        };
    }

    /**
     * \brief Provides stream-oriented file operations.
     *
     * The stream_file class template provides asynchronous and synchronous file I/O operations
     * with sequential access semantics. It maintains an internal file position that advances
     * with each read or write operation.
     *
     * \tparam IoScheduler The type of io scheduler to use for asynchronous operations.
     *
     * Example:
     * \code
     * auto file = stream_file(scheduler, "test.txt", stream_file::read_only | stream_file::create);
     * co_await file.async_write_some(buffer);
     * co_await file.async_read_some(buffer);
     * file.close();
     * \endcode
     */
    template<io_scheduler IoScheduler>
    class stream_file : public detail::stream_file_base<IoScheduler> {
    private:
        using base = detail::stream_file_base<IoScheduler>;

    public:
        using enum detail::open_mode;
        using enum detail::seek_whence;

    public:
        using base::base;

        /**
         * \brief Construct and open a stream file.
         * \param scheduler The io scheduler to use for asynchronous operations.
         * \param path The path of the file to open.
         * \param mode The mode in which to open the file (combination of open_mode flags).
         * \throw std::system_error on failure.
         */
        stream_file(IoScheduler scheduler, zstring_view path, detail::open_mode mode) : base(std::move(scheduler)) {
            open(path, mode);
        }

        /**
         * \brief Open the file at the specified path.
         * \param path The path of the file to open.
         * \param mode The mode in which to open the file. Can be a combination of:
         *   - read_only: Open for reading only
         *   - write_only: Open for writing only
         *   - read_write: Open for reading and writing
         *   - append: Append to end of file
         *   - create: Create file if it doesn't exist
         *   - exclusive: Ensure creation of a new file
         *   - truncate: Truncate existing file to zero length
         *   - sync_all_on_write: Synchronize all writes to disk
         * \throw std::system_error if the file is already open or on failure.
         */
        COIO_ALWAYS_INLINE auto open(zstring_view path, detail::open_mode mode) -> void {
            if (this->is_open()) throw std::system_error{error::already_open, "open"};
            this->impl_ = this->get_io_scheduler().make_io_object(detail::open_file(path, mode, false));
        }

        /**
         * \brief Resize the file to a specified size.
         * \param new_size The new size of the file in bytes.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto resize(std::size_t new_size) -> void {
            return detail::file_resize(this->native_handle(), new_size);
        }

        /**
         * \brief Get the current size of the file.
         * \return The size of the file in bytes.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE  auto size() const -> std::size_t {
            return detail::file_size(this->native_handle());
        }

        /**
         * \brief Set the file position indicator.
         * \param whence The position from which to seek:
         *   - seek_set: Seek from beginning of file
         *   - seek_cur: Seek from current position
         *   - seek_end: Seek from end of file
         * \return The new position from the beginning of the file.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto seek(detail::seek_whence whence) -> std::size_t {
            return detail::file_seek(this->native_handle(), whence);
        }

        /**
         * \brief Synchronize the file data and metadata with the storage device.
         *
         * This function blocks until all modified data and metadata have been written to the
         * underlying storage device.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto sync_all() -> void {
            detail::file_sync_all(this->native_handle());
        }

        /**
         * \brief Synchronize the file data with the storage device.
         *
         * This function blocks until all modified data has been written to the underlying
         * storage device. Metadata changes may not be synchronized.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto sync_data() -> void {
            detail::file_sync_data(this->native_handle());
        }
    };

    /**
     * \brief Provides random-access file operations.
     *
     * The random_access_file class template provides asynchronous and synchronous file I/O
     * operations with random access semantics. Unlike stream_file, this class allows reading
     * and writing at arbitrary positions without maintaining an internal file position.
     * All read and write operations specify an explicit offset.
     *
     * \tparam IoScheduler The type of io scheduler to use for asynchronous operations.
     *
     * Example:
     * \code
     * auto file = random_access_file(scheduler, "data.bin", random_access_file::read_write | random_access_file::create);
     * co_await file.async_write_some_at(0, buffer1);      // Write at offset 0
     * co_await file.async_write_some_at(1024, buffer2);   // Write at offset 1024
     * co_await file.async_read_some_at(512, buffer3);     // Read from offset 512
     * file.close();
     * \endcode
     */
    template<io_scheduler IoScheduler>
    class random_access_file : public detail::random_access_file_base<IoScheduler> {
    private:
        using base = detail::random_access_file_base<IoScheduler>;

    public:
        using enum detail::open_mode;
        using enum detail::seek_whence;

    public:
        using base::base;

        /**
         * \brief Construct and open a random access file.
         * \param scheduler The io scheduler to use for asynchronous operations.
         * \param path The path of the file to open.
         * \param mode The mode in which to open the file (combination of open_mode flags).
         * \throw std::system_error on failure.
         */
        random_access_file(IoScheduler scheduler, zstring_view path, detail::open_mode mode) : base(std::move(scheduler)) {
            open(path, mode);
        }

        /**
         * \brief Open the file at the specified path for random access.
         * \param path The path of the file to open.
         * \param mode The mode in which to open the file. Can be a combination of:
         *   - read_only: Open for reading only
         *   - write_only: Open for writing only
         *   - read_write: Open for reading and writing
         *   - append: Append to end of file
         *   - create: Create file if it doesn't exist
         *   - exclusive: Ensure creation of a new file
         *   - truncate: Truncate existing file to zero length
         *   - sync_all_on_write: Synchronize all writes to disk
         * \throw std::system_error if the file is already open or on failure.
         */
        COIO_ALWAYS_INLINE auto open(zstring_view path, detail::open_mode mode) -> void {
            if (this->is_open()) throw std::system_error{error::already_open, "open"};
            this->impl_ = this->get_io_scheduler().make_io_object(detail::open_file(path, mode, true));
        }

        /**
         * \brief Resize the file to a specified size.
         * \param new_size The new size of the file in bytes.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto resize(std::size_t new_size) -> void {
            return detail::file_resize(this->native_handle(), new_size);
        }

        /**
         * \brief Get the current size of the file.
         * \return The size of the file in bytes.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE  auto size() const -> std::size_t {
            return detail::file_size(this->native_handle());
        }

        /**
         * \brief Synchronize the file data and metadata with the storage device.
         *
         * This function blocks until all modified data and metadata have been written to the
         * underlying storage device.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto sync_all() -> void {
            detail::file_sync_all(this->native_handle());
        }

        /**
         * \brief Synchronize the file data with the storage device.
         *
         * This function blocks until all modified data has been written to the underlying
         * storage device. Metadata changes may not be synchronized.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto sync_data() -> void {
            detail::file_sync_data(this->native_handle());
        }
    };
}