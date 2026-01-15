#pragma once
#include <span>
#include "coio/core.h"
#include "../detail/error.h"
#include "../detail/io_descriptions.h"

struct linger;

namespace coio {
    namespace detail::socket {
        struct linger_storage {
            #if COIO_OS_WINDOWS
            using linger_integral = unsigned short;
#else
            using linger_integral = int;
#endif
            linger_integral l_onoff;
            linger_integral l_linger;
        };

        enum class shutdown_type : short {
            shutdown_send,
            shutdown_receive,
            shutdown_both,
        };

        auto set_sockopt(socket_native_handle_type handle, int level, int option_name, std::span<const std::byte> value) -> void;

        auto get_sockopt(socket_native_handle_type handle, int level, int option_name, std::span<std::byte> value) -> void;

        auto sol_socket_v() noexcept -> int;

        auto ipproto_ipv6_v() noexcept -> int;

        auto ipproto_tcp_v() noexcept -> int;

        template<typename ValueType>
        struct sock_option_traits {
            using storage = ValueType;

            static auto from_value(const ValueType& value) noexcept -> storage {
                return value;
            }

            static auto to_value(const storage& storage) noexcept -> ValueType {
                return storage;
            }
        };

        template<>
        struct sock_option_traits<bool> {
            using storage = int;

            static auto from_value(bool value) noexcept -> storage {
                return static_cast<int>(value);
            }

            static auto to_value(int storage) noexcept -> bool {
                return static_cast<bool>(storage);
            }
        };

        template<>
        struct sock_option_traits<::linger> {
            using storage = linger_storage;

            static auto from_value(const ::linger& value) noexcept -> linger_storage;

            static auto to_value(const linger_storage& storage) noexcept -> ::linger;
        };

        template<typename ValueType, int(*Level)() noexcept>
        class sock_option {
        private:
            using traits = sock_option_traits<ValueType>;
            using storage_type = typename traits::storage;
            static constexpr std::size_t length = sizeof(storage_type);

        public:
            sock_option() noexcept = default;

            explicit sock_option(ValueType value) noexcept : storage_(value) {}

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto data() noexcept -> std::span<std::byte> {
                return {reinterpret_cast<std::byte*>(&storage_), length};
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto data() const noexcept -> std::span<const std::byte> {
                return {reinterpret_cast<const std::byte*>(&storage_), length};
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE static auto level() noexcept -> int {
                return Level();
            }

            [[nodiscard]]
            auto get() const noexcept -> ValueType {
                return traits::to_value(storage_);
            }

            auto set(const ValueType& value) noexcept -> void {
                storage_ = traits::from_value(value);
            }

        protected:
            storage_type storage_;
        };

        // socket options
        struct debug : sock_option<bool, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct do_not_route : sock_option<bool, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct broadcast : sock_option<bool, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct keep_alive : sock_option<bool, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct linger : sock_option<::linger, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct out_of_band_inline : sock_option<bool, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct receive_buffer_size : sock_option<int, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct receive_low_watermark : sock_option<int, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct reuse_address : sock_option<bool, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct send_buffer_size : sock_option<int, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        struct send_low_watermark : sock_option<int, sol_socket_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        // ip options
        struct v6_only : sock_option<bool, ipproto_ipv6_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        // tcp options
        struct no_dely : sock_option<bool, ipproto_tcp_v> {
            using sock_option::sock_option;

            [[nodiscard]]
            static auto name() noexcept -> int;
        };

        [[nodiscard]]
        auto open(int family, int type, int protocol_id) -> socket_native_handle_type;

        auto close(socket_native_handle_type handle) -> void;

        [[nodiscard]]
        auto max_backlog() noexcept -> std::size_t;

        [[nodiscard]]
        auto local_endpoint(socket_native_handle_type handle) -> endpoint;

        [[nodiscard]]
        auto remote_endpoint(socket_native_handle_type handle) -> endpoint;

        auto shutdown(socket_native_handle_type handle, shutdown_type how) -> void;

        auto bind(socket_native_handle_type handle, const endpoint& local_endpoint) -> void;

        auto listen(socket_native_handle_type handle, std::size_t backlog) -> void;

        auto connect(socket_native_handle_type handle, const endpoint& peer) -> void;

        auto accept(socket_native_handle_type handle) -> socket_native_handle_type;

        auto receive(socket_native_handle_type handle, std::span<std::byte> buffer) -> std::size_t;

        auto send(socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t;

        auto receive_from(socket_native_handle_type handle, std::span<std::byte> buffer, const endpoint& src) -> std::size_t;

        auto send_to(socket_native_handle_type handle, std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t;
    }

    template<typename Protocol, io_scheduler IoScheduler>
    class basic_socket {
    private:
        using implementation_type = decltype(std::declval<IoScheduler&>().make_io_object(std::declval<detail::socket_native_handle_type>()));

    public:
        using protocol_type = Protocol;
        using scheduler_type = IoScheduler;
        using native_handle_type = detail::socket_native_handle_type;
        using shutdown_type = detail::socket::shutdown_type;
        using enum shutdown_type;

        // Socket options
        using broadcast = detail::socket::broadcast;
        using debug = detail::socket::debug;
        using do_not_route = detail::socket::do_not_route;
        using keep_alive = detail::socket::keep_alive;
        using linger = detail::socket::linger;
        using out_of_band_inline = detail::socket::out_of_band_inline;
        using receive_buffer_size = detail::socket::receive_buffer_size;
        using receive_low_watermark = detail::socket::receive_low_watermark;
        using reuse_address = detail::socket::reuse_address;
        using send_buffer_size = detail::socket::send_buffer_size;
        using send_low_watermark = detail::socket::send_low_watermark;

        // Ip options
        using v6_only = detail::socket::v6_only;

    public:
        explicit basic_socket(scheduler_type scheduler) noexcept :
            basic_socket(std::move(scheduler), detail::invalid_socket_handle) {}

        basic_socket(scheduler_type scheduler, native_handle_type handle) :
            impl_(scheduler.make_io_object(handle)) {}

        basic_socket(const basic_socket&) = delete;

        basic_socket(basic_socket&& other) = default;

        ~basic_socket() noexcept {
            close();
        }

        auto operator= (const basic_socket&) -> basic_socket& = delete;

        auto operator= (basic_socket&& other) -> basic_socket& = default;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_io_scheduler() const noexcept -> scheduler_type {
            return impl_.get_io_scheduler();
        }

        /**
         * \brief get native handle
         * \return native handle
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto native_handle() const noexcept -> native_handle_type {
            return impl_.native_handle();
        }

        /**
         * \brief open the socket using the specified protocol.
         * \param protocol an object specifying protocol parameters to be used.
        */
        COIO_ALWAYS_INLINE auto open(const protocol_type& protocol = protocol_type()) -> void {
            if (is_open()) throw std::system_error{error::already_open, "open"};
            impl_ = get_io_scheduler().make_io_object(detail::socket::open(protocol.family(), protocol.type(), protocol.protocol_id()));
        }

        /**
         * \brief close the socket. Any asynchronous send, receive or connect operations will be cancelled immediately.
         */
        COIO_ALWAYS_INLINE auto close() -> void {
            detail::socket::close(release());
        }

        /**
         * \brief release the ownership of the native handle. Any asynchronous send, receive or connect operations will be cancelled immediately.
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto release() -> native_handle_type {
            return impl_.release();
        }

        /**
         * \brief cancel all asynchronous operations associated with the socket. Any asynchronous send, receive or connect operations will be cancelled immediately.
         */
        COIO_ALWAYS_INLINE auto cancel() -> void {
            impl_.cancel();
        }

        /**
         * \brief disable sends or receives on the socket.
         * \param how `shutdown_send`: disable sends,
         * `shutdown_receive`: disable receives,
         * `shutdonw_both`: disable sends and receives.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto shutdown(shutdown_type how) -> void {
            return detail::socket::shutdown(native_handle(), how);
        }

        /**
         * \brief determine whether the socket is open
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto is_open() const noexcept -> bool {
            return native_handle() != detail::invalid_socket_handle;
        }


        /**
         * \brief same as `is_open`
         */
        COIO_ALWAYS_INLINE explicit operator bool() const noexcept {
            return is_open();
        }

        /**
         * \brief get local endpoint
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto local_endpoint() const -> endpoint {
            return detail::socket::local_endpoint(native_handle());
        }

        /**
         * \brief get remote endpoint
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto remote_endpoint() const -> endpoint {
            return detail::socket::remote_endpoint(native_handle());
        }

        /**
         * \brief set an option on the socket.
         */
        template<typename SocketOption>
        COIO_ALWAYS_INLINE auto set_option(const SocketOption& option) -> void {
            detail::socket::set_sockopt(native_handle(), option.level(), option.name(), option.data());
        }

        /**
         * \brief get an option on the socket.
         */
        template<typename SocketOption>
        COIO_ALWAYS_INLINE auto get_option(SocketOption& option) const -> void {
            detail::socket::get_sockopt(native_handle(), option.level(), option.name(), option.data());
        }

        /**
         * \brief bind the socket to the given local endpoint.
         * \param local_endpoint an endpoint on the local machine to which the socket will be bound.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto bind(const endpoint& local_endpoint) -> void {
            detail::socket::bind(native_handle(), local_endpoint);
        }

        /**
         * \brief connect the socket to the specified endpoint.
         * \param peer the remote endpoint to which the socket will be connected.
         * \throw std::system_error on failure.
        */
        COIO_ALWAYS_INLINE auto connect(const endpoint& peer) -> void {
            if (not is_open()) open();
            detail::socket::connect(native_handle(), peer);
        }

        /**
         * \brief start an asynchronous connect.
         * \param peer the remote endpoint to which the socket will be connected.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `void`.
         * \throw std::system_error on failure.
        */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_connect(const endpoint& peer, StopToken stop_token = {}) noexcept {
            if (not is_open()) open();
            return get_io_scheduler().schedule_io(impl_, detail::async_connect_t{peer}, std::move(stop_token));
        }

    protected:
        COIO_ALWAYS_INLINE auto check_handle_valid(const char* what) const -> void {
            if (not is_open()) [[unlikely]] {
                throw std::system_error{std::make_error_code(std::errc::bad_file_descriptor), what};
            }
        }

    protected:
        implementation_type impl_;
    };


    template<typename Protocol, io_scheduler IoScheduler>
    class basic_socket_acceptor : public basic_socket<Protocol, IoScheduler> {
    private:
        template<io_scheduler OtherScheduler>
        using protocol_socket_ = typename Protocol::template socket<OtherScheduler>;
        using base = basic_socket<Protocol, IoScheduler>;

    public:
        template<io_scheduler OtherScheduler>
        using rebind_scheduler = basic_socket_acceptor<Protocol, OtherScheduler>;
        using typename base::scheduler_type;
        using typename base::protocol_type;
        using typename base::native_handle_type;

    public:
        using base::base;

        basic_socket_acceptor(
            scheduler_type scheduler,
            const endpoint& local_endpoint,
            std::size_t backlog = max_backlog(),
            bool reuse_addr = true
        ) : basic_socket_acceptor(scheduler) {
            this->open(local_endpoint.ip().is_v4() ? protocol_type::v4() : protocol_type::v6());
            this->set_option(detail::socket::reuse_address{reuse_addr});
            this->bind(local_endpoint);
            this->listen(backlog);
        }

        /**
         * \brief get the maximum length of the queue of pending incoming connections.
        */
        [[nodiscard]]
        COIO_ALWAYS_INLINE static auto max_backlog() noexcept -> std::size_t {
            return detail::socket::max_backlog();
        }

        /**
         * \brief place the acceptor into the state where it will listen for new connections.
         * \param backlog the maximum length of the queue of pending connections.
         * \throw std::system_error on failure.
         */
        COIO_ALWAYS_INLINE auto listen(std::size_t backlog = max_backlog()) noexcept -> void {
           detail::socket::listen(this->native_handle(), backlog);
        }

        /**
         * \brief accept a new connection.
         * \param peer the socket into which the new connection will be accepted.
         * \throw std::system_error on failure.
         */
        template<io_scheduler OtherScheduler>
        COIO_ALWAYS_INLINE auto accept(protocol_socket_<OtherScheduler>& peer) -> void {
            peer = this->accept(peer.get_io_scheduler());
        }

        /**
         * \brief accept a new connection.
         * \param peer_scheduler the io_context object to be used for the newly accepted socket.
         * \return a socket object representing the newly accepted connection.
         * \throw std::system_error on failure.
         */
        template<io_scheduler OtherScheduler>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto accept(OtherScheduler peer_scheduler) -> protocol_socket_<OtherScheduler> {
            return protocol_socket_<OtherScheduler>(peer_scheduler, detail::socket::accept(this->native_handle()));
        }

        /**
         * \brief accept a new connection.
         * \return a socket object representing the newly accepted connection.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto accept() -> protocol_socket_<scheduler_type> {
            return this->accept(this->get_io_scheduler());
        }

        /**
         * \brief start an asynchronous accept.
         * \param peer the socket into which the new connection will be accepted.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `void`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `async_accept`, `accept` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         */
        template<io_scheduler OtherScheduler, stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_accept(protocol_socket_<OtherScheduler>& peer, StopToken stop_token = {}) {
            this->check_handle_valid("async_accept");
            return then(
                this->get_io_scheduler().schedule_io(this->impl_, detail::async_accept_t{}, std::move(stop_token)),
                [&peer](native_handle_type handle) noexcept {
                    peer = protocol_socket_<OtherScheduler>(peer.get_io_scheduler(), handle);
                }
            );
        }

        /**
         * \brief start an asynchronous accept.
         * \param other_scheduler the io_context object to be used for the newly accepted socket.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `protocol_type::socket<OtherScheduler>`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `async_accept`, `accept` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         */
        template<io_scheduler OtherScheduler, stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_accept(OtherScheduler other_scheduler, StopToken stop_token = {}) {
            this->check_handle_valid("async_accept");
            return then(
                this->get_io_scheduler().schedule_io(this->impl_, detail::async_accept_t{}, std::move(stop_token)),
                [other_scheduler](native_handle_type handle) noexcept {
                    return protocol_socket_<OtherScheduler>(other_scheduler, handle);
                }
            );
        }

        /**
         * \brief start an asynchronous accept.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `protocol_type::socket<scheduler_type>`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `async_accept`, `accept` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_accept(StopToken stop_token = {}) {
            return this->async_accept(this->get_io_scheduler(), std::move(stop_token));
        }
    };

    template<typename Protocol, io_scheduler IoScheduler>
    class basic_stream_socket : public basic_socket<Protocol, IoScheduler> {
    private:
        using base = basic_socket<Protocol, IoScheduler>;

    public:
        template<io_scheduler OtherScheduler>
        using rebind_scheduler = basic_stream_socket<Protocol, OtherScheduler>;
        using typename base::scheduler_type;
        using typename base::protocol_type;
        using typename base::native_handle_type;

    public:
        using base::base;

        /**
         * \brief read some data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
         * \note consider using `read` if you need to ensure that the requested amount of data is read before the blocking operation completes.
        */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto read_some(std::span<std::byte> buffer) -> std::size_t {
            const auto bytes_transferred = detail::socket::receive(this->native_handle(), buffer);
            if (bytes_transferred == 0) [[unlikely]] throw std::system_error{error::eof, "read_some"};
            return bytes_transferred;
        }

        /**
         * \brief write some data to the socket.
         * \param buffer data buffer to be written to the socket.
         * \return the number of bytes written.
         * \throw std::system_error on failure.
         * \note consider using `write` if you need to ensure that all data is written before the blocking operation completes.
        */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto write_some(std::span<const std::byte> buffer) -> std::size_t {
            return detail::socket::send(this->native_handle(), buffer);
        }

        /**
         * \brief same as `read_some`
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto receive(std::span<std::byte> buffer) -> std::size_t {
            return read_some(buffer);
        }

        /**
         * \brief same as `write_some`
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto send(std::span<const std::byte> buffer) -> std::size_t {
            return write_some(buffer);
        }

        /**
         * \brief receive some message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `std::size_t`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `read`, `read_some`, `receive`,
         * `async_read`, `async_read_some` or `async_receive` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         * 3) consider using `async_read` if you need to ensure that the requested amount of data is read before the asynchronous operation completes.
        */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_read_some(std::span<std::byte> buffer, StopToken stop_token = {}) {
            this->check_handle_valid("async_read_some");
            return then(
                this->get_io_scheduler().schedule_io(
                    this->impl_,
                    detail::async_receive_t{buffer},
                    std::move(stop_token)
                ),
                [total = buffer.size()](std::size_t bytes_transferred) -> std::size_t {
                    if (bytes_transferred == 0 and total > 0) [[unlikely]] {
                        throw std::system_error{error::eof, "async_read_some"};
                    }
                    return bytes_transferred;
                }
            );
        }

        /**
         * \brief send some message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `std::size_t`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `write`, `write_some`, `send`,
         * `async_write`, `async_write_some`, or `async_send` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         * 3) consider using the async_write function if you need to ensure that all data is written before the asynchronous operation completes.
        */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_write_some(std::span<const std::byte> buffer, StopToken stop_token = {}) {
            this->check_handle_valid("async_write_some");
            return this->get_io_scheduler().schedule_io(this->impl_, detail::async_send_t{buffer}, std::move(stop_token));
        }

        /**
         * \brief same as `async_read_some`
         */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_receive(std::span<std::byte> buffer, StopToken stop_token = StopToken{}) {
            return async_read_some(buffer, std::move(stop_token));
        }

        /**
         * \brief same as `async_write_some`
         */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_send(std::span<const std::byte> buffer, StopToken stop_token = StopToken{}) {
            return async_write_some(buffer, std::move(stop_token));
        }
    };

    template<typename Protocol, io_scheduler IoScheduler>
    class basic_datagram_socket : public basic_socket<Protocol, IoScheduler> {
    private:
        using base = basic_socket<Protocol, IoScheduler>;

    public:
        template<io_scheduler OtherScheduler>
        using rebind_scheduler = basic_datagram_socket<Protocol, OtherScheduler>;
        using typename base::scheduler_type;
        using typename base::protocol_type;
        using typename base::native_handle_type;

    public:
        using base::base;

        /**
         * \brief read data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto receive(std::span<std::byte> buffer) -> std::size_t {
            return detail::socket::receive(this->native_handle(), buffer);
        }

        /**
         * \brief write data to the socket.
         * \param buffer data buffer to be written to the socket.
         * \return the number of bytes written.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto send(std::span<const std::byte> buffer) -> std::size_t {
            return detail::socket::send(this->native_handle(), buffer);
        }

        /**
         * \brief read data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \param peer an endpoint object that receives the endpoint of the remote sender of the datagram.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto receive_from(std::span<std::byte> buffer, const endpoint& peer) -> std::size_t {
            return detail::socket::receive_from(this->native_handle(), buffer, peer, false);
        }

        /**
         * \brief write data to the socket.
         * \param buffer data buffer to be written to the socket.
         * \param peer the remote endpoint to which the data will be sent.
         * \return the number of bytes written.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto send_to(std::span<const std::byte> buffer, const endpoint& peer) -> std::size_t {
            return detail::socket::send_to(this->native_handle(), buffer, peer);
        }

        /**
         * \brief receive message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `std::size_t`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `receive`, `receive_from`, `async_receive`, or
         * `async_receive_from` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         * 3) consider using `async_read` if you need to ensure that the requested amount of data is read before the asynchronous operation completes.
        */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_receive(std::span<std::byte> buffer, StopToken stop_token = {}) {
            this->check_handle_valid("async_receive");
            return this->get_io_scheduler().schedule_io(
                this->impl_,
                detail::async_receive_t{buffer},
                std::move(stop_token)
            );
        }

        /**
         * \brief send message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `std::size_t`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `send`, `send_to`, `async_send`, or
         * `async_send_to` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
        */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_send(std::span<const std::byte> buffer, StopToken stop_token = {}) {
            this->check_handle_valid("async_send");
            return this->get_io_scheduler().schedule_io(
                this->impl_,
                detail::async_send_t{buffer},
                std::move(stop_token)
            );
        }

        /**
         * \brief receive message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \param peer an endpoint object that receives the endpoint of the remote sender of the datagram.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `std::size_t`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `receive`, `receive_from`, `async_receive`, or
         * `async_receive_from` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
         */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_receive_from(std::span<std::byte> buffer, const endpoint& peer, StopToken stop_token = {}) {
            this->check_handle_valid("async_receive_from");
            return this->get_io_scheduler().schedule_io(
                this->impl_,
                detail::async_receive_from_t{buffer, peer},
                std::move(stop_token)
            );
        }

        /**
         * \brief send message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \param peer the remote endpoint to which the data will be sent.
         * \param stop_token a stoppable token to cancel the operation (default: never_stop_token).
         * \return an awaitable of `std::size_t`.
         * \throw std::system_error on failure.
         * \note
         * 1) the program must ensure that no other calls to `send`, `send_to`, `async_send`, or
         * `async_send_to` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
         */
        template<stoppable_token StopToken = never_stop_token>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_send_to(std::span<const std::byte> buffer, const endpoint& peer, StopToken stop_token = {}) {
            this->check_handle_valid("async_send_to");
            return this->get_io_scheduler().schedule_io(
                this->impl_,
                detail::async_send_to_t{buffer, peer},
                std::move(stop_token)
            );
        }
    };

}
