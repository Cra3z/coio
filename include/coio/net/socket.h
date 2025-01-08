#pragma once
#include <span>
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

        [[nodiscard]]
        static auto v4() noexcept -> tcp;

        [[nodiscard]]
        static auto v6() noexcept -> tcp;

        [[nodiscard]]
        auto family() const noexcept -> int {
            return family_;
        }

        [[nodiscard]]
        static auto type() noexcept -> int;

        [[nodiscard]]
        static auto protocol_id() noexcept -> int;

    private:
        int family_;
    };

    class udp {
    private:
        explicit udp(int family) noexcept : family_(family) {}

    public:

        [[nodiscard]]
        static auto v4() noexcept -> udp;

        [[nodiscard]]
        static auto v6() noexcept -> udp;

        [[nodiscard]]
        auto family() const noexcept -> int {
            return family_;
        }

        [[nodiscard]]
        static auto type() noexcept -> int;

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

        struct op_list;

        class async_io_operation_base {
        public:
            async_io_operation_base(io_context& context, op_list* op_list_) noexcept : context_(context), op_list_(op_list_) {
                context_.work_started();
            }

            async_io_operation_base(const async_io_operation_base&) = delete;

            ~async_io_operation_base() {
                context_.work_finished();
            }

            auto operator= (const async_io_operation_base&) ->async_io_operation_base& = delete;

        protected:
            io_context& context_;
            op_list* op_list_;
            std::exception_ptr exception_;
        };

        class async_in_operation_base : public async_io_operation_base {
            friend op_list;
        public:
            using async_io_operation_base::async_io_operation_base;

            auto await_suspend(std::coroutine_handle<> this_coro) -> void;

        private:
            async_in_operation_base* next_ = nullptr;
            std::coroutine_handle<> waiter_;
        };

        class async_out_operation_base : public async_io_operation_base {
            friend op_list;
        public:
            using async_io_operation_base::async_io_operation_base;

            auto await_suspend(std::coroutine_handle<> this_coro) -> void;

        private:
            async_out_operation_base* next_ = nullptr;
            std::coroutine_handle<> waiter_;
        };

        struct op_list {
            auto add_one_in_op(async_in_operation_base* op) noexcept -> void {
                auto old_tail = std::exchange(in_op_tail, op);
                if (old_tail) old_tail->next_ = in_op_tail;
                if (in_op_head == nullptr) in_op_head = in_op_tail;
                ++length;
            }

            auto add_one_out_op(async_out_operation_base* op) noexcept -> void {
                auto old_tail = std::exchange(out_op_tail, op);
                if (old_tail) old_tail->next_ = out_op_tail;
                if (out_op_head == nullptr) out_op_head = out_op_tail;
                ++length;
            }

            auto end_one_in_op() noexcept -> std::size_t {
                if (in_op_head == in_op_tail) in_op_head = in_op_tail = nullptr;
                else in_op_head = in_op_head->next_;
                return --length;
            }

            auto end_one_out_op() noexcept -> std::size_t {
                if (out_op_head == out_op_tail) out_op_head = out_op_tail = nullptr;
                else out_op_head = out_op_head->next_;
                return --length;
            }

            auto reset(socket_native_handle_type new_handle) noexcept -> void {
                handle = new_handle;
                length = 0;
                in_op_head = in_op_tail = nullptr;
                out_op_head = out_op_tail = nullptr;
            }

            auto out_op_waiter() noexcept -> std::coroutine_handle<> {
                COIO_DCHECK(out_op_head and out_op_head->waiter_);
                return out_op_head->waiter_;
            }

            auto in_op_waiter() noexcept -> std::coroutine_handle<> {
                COIO_DCHECK(in_op_head and in_op_head->waiter_);
                return in_op_head->waiter_;
            }

            socket_native_handle_type handle{invalid_socket_handle_value};
            std::size_t length{};
            async_in_operation_base* in_op_head{nullptr}, *in_op_tail{nullptr};
            async_out_operation_base* out_op_head{nullptr}, *out_op_tail{nullptr};
        };
    }

    class tcp_socket;

    class async_receive_operation : public detail::async_in_operation_base {
    public:
        async_receive_operation(io_context& context, detail::op_list* op_list, std::span<std::byte> buffer, bool zero_as_eof) noexcept :
            async_in_operation_base{context, op_list}, buffer_(buffer), zero_as_eof_(zero_as_eof) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<std::byte> buffer_;
        /* const */ bool zero_as_eof_;
        std::size_t transferred_ = 0;
    };


    class async_send_operation : public detail::async_out_operation_base {
    public:
        async_send_operation(io_context& context, detail::op_list* op_list, std::span<const std::byte> buffer) noexcept :
            async_out_operation_base{context, op_list}, buffer_(buffer) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<const std::byte> buffer_;
        std::size_t transferred_ = 0;
    };


    class async_receive_from_operation : public detail::async_in_operation_base {
    public:
        async_receive_from_operation(io_context& context, detail::op_list* op_list, std::span<std::byte> buffer, const endpoint& src, bool zero_as_eof) noexcept :
            async_in_operation_base{context, op_list}, buffer_(buffer), src_(src), zero_as_eof_(zero_as_eof) {}

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
        async_send_to_operation(io_context& context, detail::op_list* op_list, std::span<const std::byte> buffer, const endpoint& dest) noexcept :
            async_out_operation_base{context, op_list}, buffer_(buffer), dest_(dest) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<const std::byte> buffer_;
        endpoint dest_;
        std::size_t transferred_ = 0;
    };


    class async_accept_operation : public detail::async_in_operation_base {
    public:
        async_accept_operation(io_context& context, detail::op_list* op_list, tcp_socket& out) noexcept :
            async_in_operation_base(context, op_list), out_(out) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> void;

    private:
        tcp_socket& out_;
        detail::socket_native_handle_type accepted_ = detail::invalid_socket_handle_value;
    };


    namespace detail {

        class socket_base {
        public:
            using native_handle_type = socket_native_handle_type;

        private:
            socket_base(std::nullptr_t, io_context& context, socket_native_handle_type handle) noexcept : context_(&context), handle_(handle), op_list_(nullptr) {}

        public:
            explicit socket_base(io_context& context) noexcept : socket_base(nullptr, context, invalid_socket_handle_value) {}

            socket_base(io_context& context, socket_native_handle_type handle) : socket_base(nullptr, context, handle) {
                op_list_ = new op_list{handle};
            }

            socket_base(const socket_base&) = delete;

            socket_base(socket_base&& other) noexcept : context_(other.context_),
                                                        handle_(std::exchange(other.handle_, invalid_socket_handle_value)),
                                                        op_list_(std::exchange(other.op_list_, nullptr)) {}

            ~socket_base() {
                close();
            }

            auto operator= (socket_base other) noexcept -> socket_base& {
                std::swap(context_, other.context_);
                std::swap(handle_, other.handle_);
                std::swap(op_list_, other.op_list_);
                return *this;
            }

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

            explicit operator bool() const noexcept {
                return is_open();
            }

            /**
             * \brief check whether is the socket open
             */
            [[nodiscard]]
            auto is_open() const noexcept -> bool {
                return native_handle() != invalid_socket_handle_value;
            }

            auto close() noexcept -> void;

            auto reuse_address() -> void;

            auto set_non_blocking() -> void;

            auto bind(const endpoint& addr) -> void;

        protected:
            auto reset_(native_handle_type new_handle) noexcept -> void;

            auto open_(int family, int type, int protocol_id) -> void;

            auto connect_(const endpoint& addr) -> void;

        protected:
            io_context* context_;
            socket_native_handle_type handle_;
            op_list* op_list_;
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

        [[nodiscard]]
        static auto max_backlog() noexcept -> std::size_t;

        auto open(const protocol_type& protocol) -> void {
            open_(protocol.family(), protocol.type(), protocol.protocol_id());
        }

        auto listen(std::size_t backlog = max_backlog()) -> void;

        [[nodiscard]]
        auto async_accept(tcp_socket& out) noexcept -> async_accept_operation {
            return {*context_, op_list_, out};
        }

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
        using protocol_type = tcp;

    public:
        using socket_base::socket_base;
        using socket_base::context;
        using socket_base::native_handle;
        using socket_base::local_endpoint;
        using socket_base::remote_endpoint;
        using socket_base::is_open;
        using socket_base::operator bool;
        using socket_base::close;
        using socket_base::reuse_address;
        using socket_base::set_non_blocking;
        using socket_base::bind;

        auto open(const protocol_type& protocol) -> void {
            open_(protocol.family(), protocol.type(), protocol.protocol_id());
        }

        auto connect(const endpoint& addr) -> void;

        [[nodiscard]]
        auto read_some(std::span<std::byte> buffer) -> std::size_t {
            return detail::sync_recv(handle_, buffer, true);
        }

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
         * \brief async-receive data
         * \param buffer data
         * \return async_read_some_operation
         * \note the behavior is undefined, if `async_write_some` without waiting for the previous `async_read_some` or `coio::async_read` of the same socket to be finished
         * \see async_read_some_operation
         * \see async_read
        */
        [[nodiscard]]
        auto async_read_some(std::span<std::byte> buffer) noexcept -> async_receive_operation {
            return {*context_, op_list_, buffer, true};
        }

        /**
         * \brief async-send data
         * \param buffer data
         * \return async_write_some_operation
         * \note the behavior is undefined, if `async_write_some` without waiting for the previous `async_write_some` or `coio::async_write` of the same socket to be finished
         * \see async_write_some_operation
         * \see async_write
        */
        [[nodiscard]]
        auto async_write_some(std::span<const std::byte> buffer) noexcept -> async_send_operation {
            return {*context_, op_list_, buffer};
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
        using protocol_type = udp;

    public:
        using socket_base::socket_base;
        using socket_base::context;
        using socket_base::native_handle;
        using socket_base::local_endpoint;
        using socket_base::remote_endpoint;
        using socket_base::is_open;
        using socket_base::operator bool;
        using socket_base::close;
        using socket_base::reuse_address;
        using socket_base::set_non_blocking;
        using socket_base::bind;

        auto open(const protocol_type& protocol) -> void {
            open_(protocol.family(), protocol.type(), protocol.protocol_id());
        }

        auto connect(const endpoint& addr) -> void;

        [[nodiscard]]
        auto receive(std::span<std::byte> buffer) -> std::size_t {
            return detail::sync_recv(handle_, buffer, false);
        }

        [[nodiscard]]
        auto send(std::span<const std::byte> buffer) -> std::size_t {
            return detail::sync_send(handle_, buffer);
        }

        [[nodiscard]]
        auto receive_from(std::span<std::byte> buffer, const endpoint& src) -> std::size_t {
            return detail::sync_recv_from(handle_, buffer, src, false);
        }

        [[nodiscard]]
        auto send_to(std::span<const std::byte> buffer, const endpoint& dest) -> std::size_t {
            return detail::sync_send_to(handle_, buffer, dest);
        }

        [[nodiscard]]
        auto async_receive(std::span<std::byte> buffer) -> async_receive_operation {
            return {*context_, op_list_, buffer, false};
        }

        [[nodiscard]]
        auto async_send(std::span<const std::byte> buffer) -> async_send_operation {
            return {*context_, op_list_, buffer};
        }

        [[nodiscard]]
        auto async_receive_from(std::span<std::byte> buffer, const endpoint& src) noexcept -> async_receive_from_operation {
            return {*context_, op_list_, buffer, src, false};
        }

        [[nodiscard]]
        auto async_send_to(std::span<const std::byte> buffer, const endpoint& dest) noexcept -> async_send_to_operation {
            return {*context_, op_list_, buffer, dest};
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
    }


    template<typename Protocol>
    class resolver {
    public:
        using protocol_type = Protocol;
        using query_t = detail::resolve_query_t;
        using result_t = detail::resolve_result_t;

    public:

        resolver() = default;

        explicit resolver(protocol_type protocol) noexcept(std::is_nothrow_move_constructible_v<protocol_type>) : protocol_(std::move(protocol)) {}

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
