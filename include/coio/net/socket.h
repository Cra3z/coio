#pragma once
#include <span>
#include <map>
#if COIO_OS_WINSOWS
#include <BaseTsd.h>
#endif

#include "../core.h"
#include "../async_io.h"
#include "../generator.h"
#include "ip.h"
#include "detail/error_code.h"

namespace coio::net {

    class tcp {
    private:
        explicit tcp(int family) noexcept : family_(family) {}

    public:

        /**
         * \brief construct to represent the IPv4 TCP protocol.
         */
        [[nodiscard]]
        static auto v4() noexcept -> tcp;

        /**
         * \brief construct to represent the IPv6 TCP protocol.
         */
        [[nodiscard]]
        static auto v6() noexcept -> tcp;

        /**
         * \brief get the identifier for the protocol family.
         */
        [[nodiscard]]
        auto family() const noexcept -> int {
            return family_;
        }

        /**
         * \brief get the identifier for the type of the protocol.
         */
        [[nodiscard]]
        static auto type() noexcept -> int;

        /**
         * \brief get the identifier for the protocol.
         */
        [[nodiscard]]
        static auto protocol_id() noexcept -> int;

    private:
        int family_;
    };

    class udp {
    private:
        explicit udp(int family) noexcept : family_(family) {}

    public:

        /**
         * \brief construct to represent the IPv4 UDP protocol.
         */
        [[nodiscard]]
        static auto v4() noexcept -> udp;

        /**
         * \brief construct to represent the IPv6 UDP protocol.
         */
        [[nodiscard]]
        static auto v6() noexcept -> udp;

        /**
         * \brief get the identifier for the protocol family.
         */
        [[nodiscard]]
        auto family() const noexcept -> int {
            return family_;
        }

        /**
         * \brief get the identifier for the type of the protocol.
         */
        [[nodiscard]]
        static auto type() noexcept -> int;

        /**
         * \brief get the identifier for the protocol.
         */
        [[nodiscard]]
        static auto protocol_id() noexcept -> int;

    private:
        int family_;
    };

    auto reverse_bytes(std::span<std::byte> bytes) noexcept -> void;

    inline constexpr auto host_to_net = []<typename T> requires std::is_trivially_copyable_v<T> (T in_host) noexcept -> T {
        static_assert(std::endian::native == std::endian::little or std::endian::native == std::endian::big);
        if constexpr (std::endian::native == std::endian::little) {
            reverse_bytes(std::span{reinterpret_cast<std::byte*>(&in_host), sizeof(T)});
        }
        return in_host;
    };

    inline constexpr auto net_to_host = []<typename T> requires std::is_trivially_copyable_v<T> (T from_net) noexcept -> T {
        static_assert(std::endian::native == std::endian::little or std::endian::native == std::endian::big);
        if constexpr (std::endian::native == std::endian::little) {
            reverse_bytes(std::span{reinterpret_cast<std::byte*>(&from_net), sizeof(T)});
        }
        return from_net;
    };

    namespace detail {

#if COIO_OS_LINUX
        using socket_native_handle_type = int;
#elif COIO_OS_WINDOWS
        using socket_native_handle_type = ::UINT_PTR;
#endif
        inline constexpr socket_native_handle_type invalid_socket_handle_value = socket_native_handle_type(-1);

        auto sync_recv(socket_native_handle_type handle, std::span<std::byte> buffer, bool zero_as_eof) -> std::size_t;

        auto sync_send(socket_native_handle_type handle, std::span<const std::byte> buffer) -> std::size_t;

        auto sync_recv_from(socket_native_handle_type handle, std::span<std::byte> buffer, const endpoint& src, bool zero_as_eof) -> std::size_t;

        auto sync_send_to(socket_native_handle_type handle, std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t;

        struct op_state;

        class async_io_operation_base : public io_context::async_operation_base {
        public:
            async_io_operation_base(io_context& context, op_state* op_state_) noexcept : async_operation_base(context), op_state_(op_state_) {
                context.work_started();
            }

            async_io_operation_base(const async_io_operation_base&) = delete;

            ~async_io_operation_base() {
                context_.work_finished();
            }

            auto operator= (const async_io_operation_base&) ->async_io_operation_base& = delete;

            auto context() const noexcept -> io_context& {
                return static_cast<io_context&>(context_);
            }

        protected:
            op_state* op_state_;
            std::exception_ptr exception_;
        };

        class async_in_operation_base : public async_io_operation_base {
            friend op_state;
        public:
            using async_io_operation_base::async_io_operation_base;

            auto await_suspend(std::coroutine_handle<> this_coro) -> void;
        };

        class async_out_operation_base : public async_io_operation_base {
            friend op_state;
        public:
            using async_io_operation_base::async_io_operation_base;

            auto await_suspend(std::coroutine_handle<> this_coro) -> void;
        };

        struct op_state {
            op_state(socket_native_handle_type handle) noexcept : handle(handle) {};

            op_state(const op_state&) = delete;

            auto operator= (const op_state&) -> op_state& = delete;

            auto reset(socket_native_handle_type new_handle) noexcept -> void {
                handle = new_handle;
                in_op = nullptr;
                out_op = nullptr;
            }

            socket_native_handle_type handle{invalid_socket_handle_value};
            async_in_operation_base* in_op{nullptr};
            async_out_operation_base* out_op{nullptr};
        };
    }

    class tcp_socket;

    class async_receive_operation : public detail::async_in_operation_base {
    public:
        async_receive_operation(io_context& context, detail::op_state* op_state, std::span<std::byte> buffer, bool zero_as_eof) noexcept :
            async_in_operation_base{context, op_state}, buffer_(buffer), zero_as_eof_(zero_as_eof) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<std::byte> buffer_;
        /* const */ bool zero_as_eof_;
        std::size_t transferred_ = 0;
    };


    class async_send_operation : public detail::async_out_operation_base {
    public:
        async_send_operation(io_context& context, detail::op_state* op_state, std::span<const std::byte> buffer) noexcept :
            async_out_operation_base{context, op_state}, buffer_(buffer) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<const std::byte> buffer_;
        std::size_t transferred_ = 0;
    };


    class async_receive_from_operation : public detail::async_in_operation_base {
    public:
        async_receive_from_operation(io_context& context, detail::op_state* op_state, std::span<std::byte> buffer, const endpoint& src, bool zero_as_eof) noexcept :
            async_in_operation_base{context, op_state}, buffer_(buffer), src_(src), zero_as_eof_(zero_as_eof) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<std::byte> buffer_;
        endpoint src_;
        bool zero_as_eof_;
        std::size_t transferred_ = 0;
    };


    class async_send_to_operation : public detail::async_out_operation_base {
    public:
        async_send_to_operation(io_context& context, detail::op_state* op_state, std::span<const std::byte> buffer, const endpoint& dest) noexcept :
            async_out_operation_base{context, op_state}, buffer_(buffer), dest_(dest) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<const std::byte> buffer_;
        endpoint dest_;
        std::size_t transferred_ = 0;
    };


    class async_accept_operation : public detail::async_in_operation_base {
    public:
        async_accept_operation(io_context& context, detail::op_state* op_state, tcp_socket& out) noexcept :
            async_in_operation_base(context, op_state), out_(out) {}

        static auto await_ready() noexcept -> bool {
            return false;
        }

        auto await_suspend(std::coroutine_handle<> this_coro) -> void;

        auto await_resume() -> void;

    private:
        tcp_socket& out_;
        detail::socket_native_handle_type accepted_ = detail::invalid_socket_handle_value;
    };

    class async_connect_operation : public detail::async_out_operation_base {
    public:
        async_connect_operation(io_context& context, detail::op_state* op_state, const endpoint& dest_) noexcept :
            async_out_operation_base(context, op_state), dest_(dest_) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> void;

    private:
        endpoint dest_;
        bool connected_ = false;
    };


    namespace detail {

        class socket_base {
        public:
            using native_handle_type = socket_native_handle_type;

            enum class shutdown_type : short {
                shutdown_send,
                shutdown_receive,
                shutdown_both,
            };

        private:
            socket_base(std::nullptr_t, io_context& context, socket_native_handle_type handle) noexcept : context_(&context), handle_(handle), op_state_(nullptr) {}

        public:
            explicit socket_base(io_context& context) noexcept : socket_base(nullptr, context, invalid_socket_handle_value) {}

            socket_base(io_context& context, socket_native_handle_type handle) : socket_base(nullptr, context, handle) {
                op_state_ = new op_state{handle};
            }

            socket_base(const socket_base&) = delete;

            socket_base(socket_base&& other) noexcept : context_(other.context_),
                                                        handle_(std::exchange(other.handle_, invalid_socket_handle_value)),
                                                        op_state_(std::exchange(other.op_state_, nullptr)) {}

            ~socket_base() {
                close();
            }

            auto operator= (socket_base other) noexcept -> socket_base& {
                std::swap(context_, other.context_);
                std::swap(handle_, other.handle_);
                std::swap(op_state_, other.op_state_);
                return *this;
            }


            /**
             * \brief get associated `io_context`.
             * \return associated `io_context`.
             */
            [[nodiscard]]
            auto context() const noexcept -> io_context& {
                COIO_DCHECK(context_);
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
                return native_handle() != invalid_socket_handle_value;
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
             * \brief sets the non-blocking mode of the socket.
             * \throw std::system_error on failure.
             * \note the socket's synchronous operations will throw \n
             * `std::system_error` with `operation_would_block` or
             * `resource_unavailable_try_again`
             *  if they are unable to perform the requested operation immediately.
             */
            auto set_non_blocking() -> void;

            /**
             * \brief bind the socket to the given local endpoint.
             * \param addr an endpoint on the local machine to which the socket will be bound.
             * \throw std::system_error on failure.
             */
            auto bind(const endpoint& addr) -> void;

        protected:
            auto reset_(native_handle_type new_handle) noexcept -> void;

            auto open_(int family, int type, int protocol_id) -> void;

            auto connect_(const endpoint& addr) -> void;

            auto async_connect_(const endpoint& addr) noexcept -> async_connect_operation {
                return {*context_, op_state_, addr};
            }

        protected:
            io_context* context_;
            socket_native_handle_type handle_;
            op_state* op_state_;
        };
    }

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

        auto listen(std::size_t backlog = max_backlog()) -> void;

        /**
         * \brief start an asynchronous accept.
         * \param out the socket into which the new connection will be accepted.
         * \return the awaiter - `async_accept_operation`
         */
        [[nodiscard]]
        auto async_accept(tcp_socket& out) noexcept -> async_accept_operation {
            return {*context_, op_state_, out};
        }

        /**
         * \brief start an asynchronous accept.
         * \param out the socket into which the new connection will be accepted.
         * \param token a cancellation token.
         * \return the awaiter - `async_accept_operation`
         */
        [[nodiscard]]
        auto async_accept(tcp_socket& out, std::stop_token token) -> async_accept_operation {
            if (token.stop_requested()) throw operation_stopped{};
            return async_accept(out);
        }
    };

    class tcp_socket : detail::socket_base {
        friend tcp_acceptor;
        friend async_accept_operation;

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
         * \return the awaiter - `async_connect_operation`.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        auto async_connect(const endpoint& addr) -> async_connect_operation;

        /**
         * \brief read some data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
         * \note consider using `read` if you need to ensure that the requested amount of data is read before the blocking operation completes.
        */
        [[nodiscard]]
        auto read_some(std::span<std::byte> buffer) -> std::size_t {
            return detail::sync_recv(handle_, buffer, true);
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
            return detail::sync_send(handle_, buffer);
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
         * \return the awaiter - `async_receive_operation`.
         * \note
         * 1) the program must ensure that no other calls to `read`, `read_some`, `receive`,
         * `async_read`, `async_read_some` or `async_receive` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         * 3) consider using `async_read` if you need to ensure that the requested amount of data is read before the asynchronous operation completes.
        */
        [[nodiscard]]
        auto async_read_some(std::span<std::byte> buffer) noexcept -> async_receive_operation {
            return {*context_, op_state_, buffer, true};
        }

        /**
         * \brief send some message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \return the awaiter - `async_send_operation`.
         * \note
         * 1) the program must ensure that no other calls to `write`, `write_some`, `send`,
         * `async_write`, `async_write_some`, or `async_send` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.\n
         * 3) consider using the async_write function if you need to ensure that all data is written before the asynchronous operation completes.
        */
        [[nodiscard]]
        auto async_write_some(std::span<const std::byte> buffer) noexcept -> async_send_operation {
            return {*context_, op_state_, buffer};
        }

        /**
         * \brief same as `async_read_some`
         */
        [[nodiscard]]
        auto async_receive(std::span<std::byte> buffer) noexcept -> async_receive_operation {
            return async_read_some(buffer);
        }

        /**
         * \brief same as `async_write_some`
         */
        [[nodiscard]]
        auto async_send(std::span<const std::byte> buffer) noexcept -> async_send_operation {
            return async_write_some(buffer);
        }
    };


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
         * \return the awaiter - `async_connect_operation`.
         * \throw std::system_error on failure.
         */
        [[nodiscard]]
        auto async_connect(const endpoint& addr) -> async_connect_operation;

        /**
         * \brief read data to the socket.
         * \param buffer data buffer to be read to the socket.
         * \return the number of bytes read.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        auto receive(std::span<std::byte> buffer) -> std::size_t {
            return detail::sync_recv(handle_, buffer, false);
        }

        /**
         * \brief write data to the socket.
         * \param buffer data buffer to be written to the socket.
         * \return the number of bytes written.
         * \throw std::system_error on failure.
        */
        [[nodiscard]]
        auto send(std::span<const std::byte> buffer) -> std::size_t {
            return detail::sync_send(handle_, buffer);
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
            return detail::sync_recv_from(handle_, buffer, src, false);
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
            return detail::sync_send_to(handle_, buffer, dest);
        }

        /**
         * \brief receive message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \return the awaiter - `async_receive_operation`.
         * \note
         * 1) the program must ensure that no other calls to `receive`, `receive_from`, `async_receive`, or
         * `async_receive_from` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
        */
        [[nodiscard]]
        auto async_receive(std::span<std::byte> buffer) noexcept -> async_receive_operation {
            return {*context_, op_state_, buffer, false};
        }

        /**
         * \brief send message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \return the awaiter - `async_send_operation`.
         * \note
         * 1) the program must ensure that no other calls to `send`, `send_to`, `async_send`, or
         * `async_send_to` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
        */
        [[nodiscard]]
        auto async_send(std::span<const std::byte> buffer) noexcept -> async_send_operation {
            return {*context_, op_state_, buffer};
        }

        /**
         * \brief receive message data asynchronously.
         * \param buffer the buffers containing the message part to receive.
         * \param src an endpoint object that receives the endpoint of the remote sender of the datagram.
         * \return the awaiter - `async_receive_from_operation`.
         * \note
         * 1) the program must ensure that no other calls to `receive`, `receive_from`, `async_receive`, or
         * `async_receive_from` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
         */
        [[nodiscard]]
        auto async_receive_from(std::span<std::byte> buffer, const endpoint& src) noexcept -> async_receive_from_operation {
            return {*context_, op_state_, buffer, src, false};
        }

        /**
         * \brief send message data asynchronously.
         * \param buffer the buffers containing the message part to send.
         * \param dest the remote endpoint to which the data will be sent.
         * \return the awaiter - `async_send_to_operation`.
         * \note
         * 1) the program must ensure that no other calls to `send`, `send_to`, `async_send`, or
         * `async_send_to` are performed until this operation completes.\n
         * 2) the behavior is undefined if call two initiating functions (names that start with async_)
         *  on the same socket object from different threads simultaneously.
         */
        [[nodiscard]]
        auto async_send_to(std::span<const std::byte> buffer, const endpoint& dest) noexcept -> async_send_to_operation {
            return {*context_, op_state_, buffer, dest};
        }

    };

    namespace detail {
        struct resolve_query_t {
            static const int canonical_name;
            static const int passive;
            static const int numeric_host;
            static const int numeric_service;
            static const int v4_mapped;
            static const int all_matching;
            static const int address_configured;

            std::string host_name;
            std::string service_name;
            int         flags{v4_mapped | address_configured};
        };

        struct resolve_result_t {
            class endpoint endpoint;
            std::string canonical_name;
        };

        auto resolve_impl(resolve_query_t query, int family, int socktype, int protocol_id) -> generator<resolve_result_t>;

        auto resolve_impl(resolve_query_t query, int sock_type, int protocol_id) -> generator<resolve_result_t>;

        inline auto parse_path(std::string_view str) -> std::string {
            return std::string(str);
        }

        inline auto parse_host_and_port(std::string_view str) -> std::pair<std::string, std::uint16_t> {
            std::pair<std::string, std::uint16_t> result{};
            auto pos = str.find(':');
            result.first = std::string(str.substr(0, pos));
            if (pos == std::string_view::npos) return result;
            auto port_str = str.substr(pos + 1);
            auto ec = std::from_chars(port_str.data(), port_str.data() + port_str.size(), result.second).ec;
            if (ec != std::errc()) throw std::invalid_argument("invalid URL: invalid port.");
            return result;
        }

        inline auto parse_query(std::string_view str) -> std::map<std::string, std::string> {
            std::map<std::string, std::string> result;
            while (not str.empty()) {
                auto eq_pos = str.find('=');
                auto amp_pos = str.find('&');

                std::string key = std::string(str.substr(0, eq_pos));
                std::string value;
                if (eq_pos != std::string_view::npos) [[likely]] {
                    value = std::string(str.substr(eq_pos + 1, amp_pos - eq_pos - 1));
                }
                result.insert({std::move(key), std::move(value)});

                if (amp_pos == std::string_view::npos) break;
                str.remove_prefix(amp_pos + 1);
            }
            return result;
        }
    }

    struct url {
    public:
        url(std::string_view uri) {
            std::size_t first = 0;
            auto last = uri.find("://", first);
            if (last == std::string_view::npos) throw std::invalid_argument("invalid URL: missing protocol.");
            protocol = std::string(uri.substr(first, last - first));
            first = last + 3;

            last = uri.find('/', first);
            std::tie(host, port) = detail::parse_host_and_port(uri.substr(first, last - first));
            if (port == 0) {
                if (protocol == "http") port = 80;
                else if (protocol == "https") port = 443;
            }
            if (last == std::string_view::npos) return;
            first = last + 1;

            last = uri.find('?', first);
            path += std::string(uri.substr(first, last - first));
            if (last == std::string_view::npos) return;
            first = last + 1;

            last = uri.find('#', first);
            query = detail::parse_query(uri.substr(first, last - first));
            if (last == std::string_view::npos) return;
            first = last + 1;

            fragment = std::string(uri.substr(first));
        }

        [[nodiscard]]
        auto to_string() const -> std::string {
            std::string result;
            result.append(protocol).append("://").append(host);
            if (not ((protocol == "http" and port == 80) or (protocol == "https" and port == 443))) {
                result.append(":" + std::to_string(port));
            }
            result.append(path);
            if (not query.empty()) {
                result.push_back('?');
                for (bool is_first = true; const auto& [key, value] : query) {
                    if (not is_first) [[likely]] result.push_back('&');
                    else is_first = false;
                    result.append(key).append("=").append(value);
                }
            }
            if (not fragment.empty()) {
                result.append("#" + fragment);
            }
            return result;
        }

        std::string                        protocol;
        std::string                        host;
        std::uint16_t                      port;
        std::string                        path{"/"};
        std::map<std::string, std::string> query;
        std::string                        fragment;
    };

    template<typename Protocol>
    class resolver {
    public:
        using protocol_type = Protocol;
        using query_t = detail::resolve_query_t;
        using result_t = detail::resolve_result_t;

    public:

        resolver() = default;

        explicit resolver(protocol_type protocol) noexcept(std::is_nothrow_move_constructible_v<protocol_type>) : protocol_(std::move(protocol)) {}

        /**
         * \brief resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        auto resolve(query_t query) const -> generator<result_t> {
            if (protocol_) return detail::resolve_impl(std::move(query), protocol_->family(), protocol_type::type(), protocol_type::protocol_id());
            return detail::resolve_impl(std::move(query), protocol_type::type(), protocol_type::protocol_id());
        }

    private:
        std::optional<protocol_type> protocol_;
    };

    using tcp_resolver = resolver<tcp>;
    using udp_resolver = resolver<udp>;
}

#ifdef __cpp_lib_format
template<>
struct std::formatter<coio::net::url> : coio::no_specification_formatter {
    auto format(const coio::net::url& uri, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", uri.to_string());
    }
};
#endif