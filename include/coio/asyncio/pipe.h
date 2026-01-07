#pragma once
#include <utility>
#include "../core.h"
#include "../detail/error.h"
#include "../detail/io_descriptions.h"

namespace coio {
    namespace detail {
#if COIO_OS_LINUX
        using pipe_native_handle_type = int;

        inline constexpr pipe_native_handle_type invalid_pipe_handle = -1;
#elif COIO_OS_WINDOWS
        using pipe_native_handle_type = void*;

        inline constexpr pipe_native_handle_type invalid_pipe_handle = nullptr;
#endif

        auto make_native_pipe() -> std::pair<pipe_native_handle_type, pipe_native_handle_type>;

        auto close_pipe(pipe_native_handle_type pipe) -> void;

        auto pipe_read(pipe_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t;

        auto pipe_write(pipe_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t;

        struct make_pipe_fn;

        template<io_scheduler IoScheduler>
        class pipe_base {
        private:
            using implementation_type = decltype(std::declval<IoScheduler&>().wrap_fd(std::declval<pipe_native_handle_type>()));

        public:
            using native_handle_type = pipe_native_handle_type;
            using scheduler_type = IoScheduler;

        public:
            explicit pipe_base(scheduler_type scheduler) noexcept : pipe_base(std::move(scheduler), invalid_pipe_handle) {}

            pipe_base(scheduler_type scheduler, native_handle_type handle) : impl_(scheduler.wrap_fd(handle)) {}

            pipe_base(const pipe_base&) = delete;

            pipe_base(pipe_base&& other) = default;

            ~pipe_base() noexcept {
                close();
            }

            auto operator= (const pipe_base&) -> pipe_base& = delete;

            auto operator= (pipe_base&& other) -> pipe_base& = default;

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler_type {
                return impl_.get_io_scheduler();
            }

            COIO_ALWAYS_INLINE auto close() -> void {
                close_pipe(release());
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto release() -> native_handle_type {
                return impl_.release();
            }

            COIO_ALWAYS_INLINE auto cancel() -> void {
                impl_.cancel();
            }

            [[nodiscard]]
            auto native_handle() const noexcept -> pipe_native_handle_type {
                return impl_.native_handle();
            }

        protected:
            implementation_type impl_;
        };
    }

    template<io_scheduler IoScheduler>
    class pipe_reader : public detail::pipe_base<IoScheduler> {
    private:
        using base = detail::pipe_base<IoScheduler>;

    public:
        using base::base;

        COIO_ALWAYS_INLINE auto read_some(std::span<std::byte> buffer) -> std::size_t {
            return detail::pipe_read(this->native_handle(), buffer);
        }

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
    };

    template<io_scheduler IoScheduler>
    class pipe_writer: public detail::pipe_base<IoScheduler> {
    private:
        using base = detail::pipe_base<IoScheduler>;

    public:
        using base::base;

        COIO_ALWAYS_INLINE auto write_some(std::span<const std::byte> buffer) -> std::size_t {
            return detail::pipe_write(this->native_handle(), buffer);
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_write_some(std::span<const std::byte> buffer) {
            return this->get_io_scheduler().schedule_io(this->impl_, detail::async_write_some_t{buffer});
        }
    };

    namespace detail {
        struct make_pipe_fn {
            template<io_scheduler ReaderIoScheduler, io_scheduler WriterIoScheduler>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                ReaderIoScheduler sched1,
                pipe_native_handle_type reader_handle,
                WriterIoScheduler sched2,
                pipe_native_handle_type writer_handle
            ) COIO_STATIC_CALL_OP_CONST -> std::pair<pipe_reader<ReaderIoScheduler>, pipe_writer<WriterIoScheduler>> {
                return {
                    std::piecewise_construct,
                    std::forward_as_tuple(std::move(sched1), reader_handle),
                    std::forward_as_tuple(std::move(sched2), writer_handle)
                };
            }

            template<io_scheduler IoScheduler>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                IoScheduler sched,
                pipe_native_handle_type reader_handle,
                pipe_native_handle_type writer_handle
            ) COIO_STATIC_CALL_OP_CONST -> std::pair<pipe_reader<IoScheduler>, pipe_writer<IoScheduler>> {
                return make_pipe_fn{}(sched, reader_handle, sched, writer_handle);
            }

            template<io_scheduler ReaderIoScheduler1, io_scheduler WriterScheduler>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (ReaderIoScheduler1 sched1, WriterScheduler sched2) COIO_STATIC_CALL_OP_CONST {
                auto [reader_handle, writer_handle] = make_native_pipe();
                return make_pipe_fn{}(std::move(sched1), reader_handle, std::move(sched2), writer_handle);
            }

            template<io_scheduler IoScheduler>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (IoScheduler sched) COIO_STATIC_CALL_OP_CONST {
                return make_pipe_fn{}(sched, sched);
            }
        };
    }

    inline constexpr detail::make_pipe_fn make_pipe{};
}