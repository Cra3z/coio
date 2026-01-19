#pragma once
#include "file.h"

namespace coio {
    template<io_scheduler IoScheduler>
    class pipe_reader : public detail::stream_file_base<IoScheduler> {
    private:
        using base = detail::stream_file_base<IoScheduler>;

    public:
        using base::base;
        using base::read_some;
        using base::async_read_some;
    };


    template<io_scheduler IoScheduler>
    class pipe_writer: public detail::stream_file_base<IoScheduler> {
    private:
        using base = detail::stream_file_base<IoScheduler>;

    public:
        using base::base;
        using base::write_some;
        using base::async_write_some;
    };

    namespace detail {
        auto make_native_pipe() -> std::pair<file_native_handle_type, file_native_handle_type>;

        struct make_pipe_fn {
            template<io_scheduler ReaderIoScheduler, io_scheduler WriterIoScheduler>
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                ReaderIoScheduler sched1,
                file_native_handle_type reader_handle,
                WriterIoScheduler sched2,
                file_native_handle_type writer_handle
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
                file_native_handle_type reader_handle,
                file_native_handle_type writer_handle
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