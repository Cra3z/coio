#pragma once
#include <span>
#include "base.h"
#include "async_operation.h"

namespace coio {

    namespace detail {
        auto receive(socket_native_handle_type handle, std::span<std::byte> buffer, bool zero_as_eof) -> std::size_t;

        auto send(socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t;

        auto receive_from(socket_native_handle_type handle, std::span<std::byte> buffer, const endpoint& src, bool zero_as_eof) -> std::size_t;

        auto send_to(socket_native_handle_type handle, std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t;
    }

    class async_accept_1_t;

    class async_accept_2_t;

    namespace detail {

        class socket_base {
        public:
            using native_handle_type = socket_native_handle_type;

            enum class shutdown_type : short {
                shutdown_send,
                shutdown_receive,
                shutdown_both,
            };

        public:
            explicit socket_base(io_context& context) noexcept : context_(&context), handle_(detail::invalid_socket_handle_value) {}

            socket_base(io_context& context, native_handle_type handle) : socket_base(context) {
                reset_(handle);
            }

            socket_base(const socket_base&) = delete;

            socket_base(socket_base&& other) noexcept :
                context_(other.context_), handle_(std::exchange(other.handle_, detail::invalid_socket_handle_value))
            {}

            ~socket_base() {
                close();
            }

            auto operator= (socket_base other) noexcept -> socket_base& {
                std::swap(context_, other.context_);
                std::swap(handle_, other.handle_);
                return *this;
            }


            /**
             * \brief get associated `io_context`.
             * \return associated `io_context`.
             */
            [[nodiscard]]
            auto context() const noexcept -> io_context& {
                COIO_ASSERT(context_);
                return *context_;
            }

            /**
             * \brief get native handle
             * \return native handle
             */
            [[nodiscard]]
            auto native_handle() const noexcept -> native_handle_type {
                return handle_;
            }

            /**
             * \brief get local endpoint
             * \return locl endpoint
             * \pre `native_handle()` is valid
             */
            [[nodiscard]]
            auto local_endpoint() const noexcept -> endpoint;

            /**
             * \brief get remote endpoint
             * \return remote endpoint
             * \pre `native_handle()` is valid
             */
            [[nodiscard]]
            auto remote_endpoint() const noexcept -> endpoint;

            /**
             * \brief determine whether the socket is open.
             */
            explicit operator bool() const noexcept {
                return is_open();
            }

            /**
             * \brief determine whether the socket is open.
             */
            [[nodiscard]]
            auto is_open() const noexcept -> bool {
                return native_handle() != detail::invalid_socket_handle_value;
            }

            /**
             * \brief close socket.
             */
            auto close() noexcept -> void;

            /**
             * \brief disable sends or receives on the socket.
             * \param how `shutdown_send`: disable sends,
             * `shutdown_receive`: disable receives,
             * `shutdonw_both`: disable sends and receives.
             * \throw std::system_error on failure.
             */
            auto shutdown(shutdown_type how) -> void;

            /**
             * \brief allow the socket to be bound to an address that is already in use.
             * \throw std::system_error on failure. 
             */
            auto reuse_address() -> void;

            /**
             * \brief set the non-blocking mode of the socket.
             * \throw std::system_error on failure.
             * \note 1) the socket's synchronous operations will throw
             * `std::system_error` with `std::errc::operation_would_block` or
             * `std::errc::resource_unavailable_try_again`
             *  if they are unable to perform the requested operation immediately.\n
             * 2) The non-blocking mode has no effect on the behaviour of asynchronous operations.
            *  Asynchronous operations will never fail with the error `std::errc::operation_would_block` or
             * `std::errc::resource_unavailable_try_again` \n
             * \sa is_non_blocking
             */
            auto set_non_blocking(bool mode) -> void;

            /**
             * \brief get the non-blocking mode of the socket.
             * \return true if the socket's synchronous operations will fail with `std::errc::operation_would_block` or `std::errc::resource_unavailable_try_again` if they are unable to perform the requested operation immediately. If false, synchronous operations will block until complete.
             * \pre `is_open()` is true
             * \sa set_non_blocking
             */
            [[nodiscard]]
            auto is_non_blocking() const noexcept -> bool;

            /**
             * \brief bind the socket to the given local endpoint.
             * \param addr an endpoint on the local machine to which the socket will be bound.
             * \throw std::system_error on failure.
             */
            auto bind(const endpoint& addr) -> void;

            auto cancel() -> void;

        protected:
            auto reset_(native_handle_type new_handle) noexcept -> void;

            auto open_(int family, int type, int protocol_id) -> void;

            auto connect_(const endpoint& addr) -> void;

            auto async_connect_(const endpoint& addr) noexcept -> async_connect_t {
                return {*context_, handle_, {addr}};
            }

        protected:
            io_context* context_;
            native_handle_type handle_;
        };
    }

    class tcp_socket;

    class tcp_acceptor : detail::socket_base {
    public:
        using socket_base::native_handle_type;
        using protocol_type = tcp;

    public:
        tcp_acceptor(io_context& context) noexcept : socket_base(context) {}

        tcp_acceptor(io_context& context, native_handle_type handle) : socket_base(context, handle) {};

        tcp_acceptor(io_context& context, const endpoint& addr, std::size_t backlog = max_backlog(), bool reuse_addr = true);

        using socket_base::context;
        using socket_base::native_handle;
        using socket_base::local_endpoint;
        using socket_base::is_open;
        using socket_base::operator bool;
        using socket_base::close;
        using socket_base::reuse_address;
        using socket_base::set_non_blocking;
        using socket_base::is_non_blocking;
        using socket_base::bind;

        /**
         * \brief get the maximum length of the queue of pending incoming connections.
        */
        [[nodiscard]]
        static auto max_backlog() noexcept -> std::size_t;

        /**
         * \brief open the socket using the specified protocol.
         * \param protocol an object specifying protocol parameters to be used. it's `tcp::v4()` or `tcp::v6()`.
        */
        auto open(const protocol_type& protocol) -> void {
            open_(protocol.family(), protocol.type(), protocol.protocol_id());
        }

        /**
         * \brief place the acceptor into the state where it will listen for new connections.
         * \param backlog the maximum length of the queue of pending connections.
         * \throw std::system_error on failure.
         */
        auto listen(std::size_t backlog = max_backlog()) -> void;

        /**
         * \brief accept a new connection.
         * \param peer the socket into which the new connection will be accepted.
         * \throw std::system_error on failure.
         */
        auto accept(tcp_socket& peer) -> void;

        /**
         * \brief accept a new connection.
         * \param context_of_peer the io_context object to be used for the newly accepted socket.
         * \return a socket object representing the newly accepted connection.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        auto accept(io_context& context_of_peer) -> tcp_socket;

        /**
         * \brief accept a new connection.
         * \return a socket object representing the newly accepted connection.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        auto accept() -> tcp_socket;

        /**
         * \brief start an asynchronous accept.
         * \param peer the socket into which the new connection will be accepted.
         * \return the awaitable - `async_accept_1_t`.
         * \note
         * 1) the program must ensure that no other calls to `async_accept`, `accept` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         */
        [[nodiscard]]
        auto async_accept(tcp_socket& peer) noexcept -> async_accept_1_t;

        /**
         * \brief start an asynchronous accept.
         * \param context_of_peer the io_context object to be used for the newly accepted socket.
         * \return the awaitable - `async_accept_2_t`.
         * \note
         * 1) the program must ensure that no other calls to `async_accept`, `accept` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         */
        [[nodiscard]]
        auto async_accept(io_context& context_of_peer) noexcept -> async_accept_2_t;

        /**
         * \brief start an asynchronous accept.
         * \return the awaitable - `async_accept_2_t`.
         * \note
         * 1) the program must ensure that no other calls to `async_accept`, `accept` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         */
        [[nodiscard]]
        auto async_accept() noexcept -> async_accept_2_t;
    };

    class tcp_socket : detail::socket_base {
        friend tcp_acceptor;
        friend async_accept_1_t;
        friend async_accept_2_t;

    public:
        using socket_base::native_handle_type;
        using socket_base::shutdown_type;
        using protocol_type = tcp;
        using enum shutdown_type;

    public:
        using socket_base::socket_base;
        using socket_base::context;
        using socket_base::native_handle;
        using socket_base::local_endpoint;
        using socket_base::remote_endpoint;
        using socket_base::is_open;
        using socket_base::operator bool;
        using socket_base::close;
        using socket_base::shutdown;
        using socket_base::reuse_address;
        using socket_base::set_non_blocking;
        using socket_base::is_non_blocking;
        using socket_base::bind;

        /**
         * \brief open the socket using the specified protocol.
         * \param protocol an object specifying protocol parameters to be used. it's `tcp::v4()` or `tcp::v6()`.
        */
        auto open(const protocol_type& protocol) -> void {
            open_(protocol.family(), protocol.type(), protocol.protocol_id());
        }

        /**
         * \brief connect the socket to the specified endpoint.
         * \param addr the remote endpoint to which the socket will be connected.
         * \throw std::system_error on failure.
        */
        auto connect(const endpoint& addr) -> void;

        /**
         * \brief start an asynchronous connect.
         * \param addr the remote endpoint to which the socket will be connected.
         * \return the awaitable - `async_connect_t`.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        auto async_connect(const endpoint& addr) -> async_connect_t;

        /**
         * \brief read some data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
         * \note consider using `read` if you need to ensure that the requested amount of data is read before the blocking operation completes.
        */
        [[nodiscard]]
        auto read_some(std::span<std::byte> buffer) -> std::size_t {
            return detail::receive(handle_, buffer, true);
        }

        /**
         * \brief write some data to the socket.
         * \param buffer data buffer to be written to the socket.
         * \return the number of bytes written.
         * \throw std::system_error on failure.
         * \note consider using `write` if you need to ensure that all data is written before the blocking operation completes.
        */
        [[nodiscard]]
        auto write_some(std::span<const std::byte> buffer) -> std::size_t {
            return detail::send(handle_, buffer);
        }

        /**
         * \brief same as `read_some`
         */
        [[nodiscard]]
        auto receive(std::span<std::byte> buffer) -> std::size_t {
            return read_some(buffer);
        }

        /**
         * \brief same as `write_some`
         */
        [[nodiscard]]
        auto send(std::span<const std::byte> buffer) -> std::size_t {
            return write_some(buffer);
        }

        /**
         * \brief receive some message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \return the awaitable - `async_receive_t`.
         * \note
         * 1) the program must ensure that no other calls to `read`, `read_some`, `receive`,
         * `async_read`, `async_read_some` or `async_receive` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         * 3) consider using `async_read` if you need to ensure that the requested amount of data is read before the asynchronous operation completes.
        */
        [[nodiscard]]
        auto async_read_some(std::span<std::byte> buffer) noexcept -> async_receive_t {
            return {*context_, handle_, {buffer, true}};
        }

        /**
         * \brief send some message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \return the awaitable - `async_send_t`.
         * \note
         * 1) the program must ensure that no other calls to `write`, `write_some`, `send`,
         * `async_write`, `async_write_some`, or `async_send` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         * 3) consider using the async_write function if you need to ensure that all data is written before the asynchronous operation completes.
        */
        [[nodiscard]]
        auto async_write_some(std::span<const std::byte> buffer) noexcept -> async_send_t {
            return {*context_, handle_, {buffer}};
        }

        /**
         * \brief same as `async_read_some`
         */
        [[nodiscard]]
        auto async_receive(std::span<std::byte> buffer) noexcept -> async_receive_t {
            return async_read_some(buffer);
        }

        /**
         * \brief same as `async_write_some`
         */
        [[nodiscard]]
        auto async_send(std::span<const std::byte> buffer) noexcept -> async_send_t {
            return async_write_some(buffer);
        }
    };

    class async_accept_1_t {
    public:
        async_accept_1_t(io_context& context, detail::socket_native_handle_type native_handle, tcp_socket& out) noexcept :
           impl_(context, native_handle, {}), peer_(&out) {}

        async_accept_1_t(const async_accept_1_t&) = delete;

        async_accept_1_t(async_accept_1_t&& other) noexcept : impl_(std::move(other.impl_)), peer_(std::exchange(other.peer_, {})) {}

        auto operator= (const async_accept_1_t& other) -> async_accept_1_t& = delete;

        auto operator= (async_accept_1_t&& other) noexcept -> async_accept_1_t& {
            impl_ = std::move(other.impl_);
            peer_ = std::exchange(other.peer_, {});
            return *this;
        }

        auto operator co_await() && noexcept {
            struct awaiter {
                auto await_ready() noexcept -> bool {
                    return base.await_ready();
                }

                auto await_suspend(std::coroutine_handle<> this_coro) {
                    return base.await_suspend(this_coro);
                }

                auto await_resume() -> void {
                    auto native_handle = base.await_resume();
                    peer.reset_(native_handle);
                }

                async_accept_t::awaiter base;
                tcp_socket& peer;
            };

            COIO_ASSERT(peer_ != nullptr);
            return awaiter{std::move(impl_).operator co_await(), *std::exchange(peer_, {})};
        }

    private:
        async_accept_t impl_;
        tcp_socket* peer_;
    };

    class async_accept_2_t {
    public:
        async_accept_2_t(io_context& context, detail::socket_native_handle_type native_handle, io_context& context_of_peer) noexcept :
           impl_(context, native_handle, {}), context_of_peer_(&context_of_peer) {}

        async_accept_2_t(const async_accept_2_t&) = delete;

        async_accept_2_t(async_accept_2_t&& other) noexcept :
            impl_(std::move(other.impl_)), context_of_peer_(std::exchange(other.context_of_peer_, {})) {}

        auto operator= (const async_accept_2_t&) -> async_accept_2_t& = delete;

        auto operator= (async_accept_2_t&& other) noexcept -> async_accept_2_t& {
            impl_ = std::move(other.impl_);
            context_of_peer_ = std::exchange(other.context_of_peer_, {});
            return *this;
        }

        auto operator co_await() && noexcept {
            struct awaiter {
                auto await_ready() noexcept -> bool {
                    return base.await_ready();
                }

                auto await_suspend(std::coroutine_handle<> this_coro) {
                    return base.await_suspend(this_coro);
                }

                auto await_resume() -> tcp_socket {
                    return {context_of_peer, base.await_resume()};
                }

                async_accept_t::awaiter base;
                io_context& context_of_peer; // not null
            };

            COIO_ASSERT(context_of_peer_ != nullptr);
            return awaiter{std::move(impl_).operator co_await(), *std::exchange(context_of_peer_, {})};
        }

    private:
        async_accept_t impl_;
        io_context* context_of_peer_;
    };

    inline auto tcp_acceptor::accept(io_context& context_of_peer) -> tcp_socket {
        tcp_socket out{context_of_peer};
        accept(out);
        return out;
    }

    inline auto tcp_acceptor::accept() -> tcp_socket {
        return accept(*context_);
    }

    inline auto tcp_acceptor::async_accept(tcp_socket& peer) noexcept -> async_accept_1_t {
        return {*context_, handle_, peer};
    }

    inline auto tcp_acceptor::async_accept(io_context& context_of_peer) noexcept -> async_accept_2_t {
        return {*context_, handle_, context_of_peer};
    }

    inline auto tcp_acceptor::async_accept() noexcept -> async_accept_2_t {
        return async_accept(*context_);
    }

    class udp_socket : detail::socket_base {
    public:
        using socket_base::native_handle_type;
        using socket_base::shutdown_type;
        using protocol_type = udp;
        using enum shutdown_type;

    public:
        using socket_base::socket_base;
        using socket_base::context;
        using socket_base::native_handle;
        using socket_base::local_endpoint;
        using socket_base::remote_endpoint;
        using socket_base::is_open;
        using socket_base::operator bool;
        using socket_base::close;
        using socket_base::shutdown;
        using socket_base::reuse_address;
        using socket_base::set_non_blocking;
        using socket_base::is_non_blocking;
        using socket_base::bind;

        /**
         * \brief open the socket using the specified protocol.
         * \param protocol an object specifying protocol parameters to be used. it's `udp::v4()` or `udp::v6()`.
         */
        auto open(const protocol_type& protocol) -> void {
            open_(protocol.family(), protocol.type(), protocol.protocol_id());
        }

        /**
         * \brief connect the socket to the specified endpoint.
         * \param addr the remote endpoint to which the socket will be connected.
         * \throw std::system_error on failure.
        */
        auto connect(const endpoint& addr) -> void;

        /**
         * \brief start an asynchronous connect.
         * \param addr the remote endpoint to which the socket will be connected.
         * \return the awaitable - `async_connect_t`.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        auto async_connect(const endpoint& addr) -> async_connect_t;

        /**
         * \brief read data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        auto receive(std::span<std::byte> buffer) -> std::size_t {
            return detail::receive(handle_, buffer, false);
        }

        /**
         * \brief write data to the socket.
         * \param buffer data buffer to be written to the socket.
         * \return the number of bytes written.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        auto send(std::span<const std::byte> buffer) -> std::size_t {
            return detail::send(handle_, buffer);
        }

        /**
         * \brief read data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \param src an endpoint object that receives the endpoint of the remote sender of the datagram.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        auto receive_from(std::span<std::byte> buffer, const endpoint& src) -> std::size_t {
            return detail::receive_from(handle_, buffer, src, false);
        }

        /**
         * \brief write data to the socket.
         * \param buffer data buffer to be written to the socket.
         * \param dest the remote endpoint to which the data will be sent.
         * \return the number of bytes written.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        auto send_to(std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t {
            return detail::send_to(handle_, buffer, dest);
        }

        /**
         * \brief receive message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \return the awaitable - `async_receive_t`.
         * \note
         * 1) the program must ensure that no other calls to `receive`, `receive_from`, `async_receive`, or
         * `async_receive_from` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
        */
        [[nodiscard]]
        auto async_receive(std::span<std::byte> buffer) noexcept -> async_receive_t {
            return {*context_, handle_, {buffer, false}};
        }

        /**
         * \brief send message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \return the awaitable - `async_send_t`.
         * \note
         * 1) the program must ensure that no other calls to `send`, `send_to`, `async_send`, or
         * `async_send_to` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
        */
        [[nodiscard]]
        auto async_send(std::span<const std::byte> buffer) noexcept -> async_send_t {
            return {*context_, handle_, {buffer}};
        }

        /**
         * \brief receive message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \param src an endpoint object that receives the endpoint of the remote sender of the datagram.
         * \return the awaitable - `async_receive_from_t`.
         * \note
         * 1) the program must ensure that no other calls to `receive`, `receive_from`, `async_receive`, or
         * `async_receive_from` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
         */
        [[nodiscard]]
        auto async_receive_from(std::span<std::byte> buffer, const endpoint& src) noexcept -> async_receive_from_t {
            return {*context_, handle_, {buffer, src, false}};
        }

        /**
         * \brief send message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \param dest the remote endpoint to which the data will be sent.
         * \return the awaitable - `async_send_to_t`.
         * \note
         * 1) the program must ensure that no other calls to `send`, `send_to`, `async_send`, or
         * `async_send_to` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
         */
        [[nodiscard]]
        auto async_send_to(std::span<const std::byte> buffer, const endpoint& dest) noexcept -> async_send_to_t {
            return {*context_, handle_, {buffer, dest}};
        }
    };

}
