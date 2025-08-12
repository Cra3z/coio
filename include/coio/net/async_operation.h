#pragma once
#include "../execution_context.h"
#include "base.h"

namespace coio {
    namespace detail {
        class async_io_operation : public execution_context::async_operation {
            friend io_context;
            friend io_context::impl;
        public:
            enum class category : unsigned char {
                input,
                output
            };

        public:
            async_io_operation(execution_context& context, socket_native_handle_type native_handle, category category) noexcept : async_operation(context), native_handle_(native_handle), category_(category) {
                context.work_started();
            }

            ~async_io_operation() {
                context_.work_finished();
            }

            auto await_suspend(std::coroutine_handle<> this_coro) -> void;

        protected:
            std::exception_ptr exception_;
            socket_native_handle_type native_handle_ = invalid_socket_handle_value;
            category category_;
            bool lazy_ = false; // indicate that this operation's `await_ready` always returns false
        };


        class async_input_operation : public async_io_operation {
        public:
            async_input_operation(io_context& context, socket_native_handle_type native_handle) noexcept : async_io_operation(context, native_handle, async_io_operation::category::input) {}
        };


        class async_output_operation : public async_io_operation {
        public:
            async_output_operation(io_context& context, socket_native_handle_type native_handle) noexcept : async_io_operation(context, native_handle, async_io_operation::category::output) {}
        };

    }


    class async_receive_operation : public detail::async_input_operation {
    public:
        async_receive_operation(io_context& context, detail::socket_native_handle_type native_handle, std::span<std::byte> buffer, bool zero_as_eof) noexcept :
            async_input_operation{context, native_handle}, buffer_(buffer), zero_as_eof_(zero_as_eof) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<std::byte> buffer_;
        /* const */ bool zero_as_eof_;
        std::size_t transferred_ = 0;
    };


    class tcp_socket;


    class async_send_operation : public detail::async_output_operation {
    public:
        async_send_operation(io_context& context, detail::socket_native_handle_type native_handle, std::span<const std::byte> buffer) noexcept :
            async_output_operation{context, native_handle}, buffer_(buffer) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<const std::byte> buffer_;
        std::size_t transferred_ = 0;
    };


    class async_receive_from_operation : public detail::async_input_operation {
    public:
        async_receive_from_operation(io_context& context, detail::socket_native_handle_type native_handle, std::span<std::byte> buffer, const endpoint& src, bool zero_as_eof) noexcept :
            async_input_operation{context, native_handle}, buffer_(buffer), src_(src), zero_as_eof_(zero_as_eof) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<std::byte> buffer_;
        endpoint src_;
        bool zero_as_eof_;
        std::size_t transferred_ = 0;
    };


    class async_send_to_operation : public detail::async_output_operation {
    public:
        async_send_to_operation(io_context& context, detail::socket_native_handle_type native_handle, std::span<const std::byte> buffer, const endpoint& dest) noexcept :
            async_output_operation{context, native_handle}, buffer_(buffer), dest_(dest) {}

        auto await_ready() noexcept -> bool;

        auto await_resume() -> std::size_t;

    private:
        std::span<const std::byte> buffer_;
        endpoint dest_;
        std::size_t transferred_ = 0;
    };


    class async_accept_operation : public detail::async_input_operation {
    public:
        async_accept_operation(io_context& context, detail::socket_native_handle_type native_handle) noexcept : async_input_operation(context, native_handle) {
            lazy_ = true;
        }

        static auto await_ready() noexcept -> bool {
            return false;
        }

    protected:
        auto on_resume_() -> detail::socket_native_handle_type;

    private:
        detail::socket_native_handle_type accepted_ = detail::invalid_socket_handle_value;
    };


    class async_connect_operation : public detail::async_output_operation {
    public:
        async_connect_operation(io_context& context, detail::socket_native_handle_type native_handle, const endpoint& dest_) noexcept :
            async_output_operation(context, native_handle), dest_(dest_) {
            lazy_ = true;
        }

        auto await_ready() noexcept -> bool;

        auto await_resume() -> void;

    private:
        endpoint dest_;
    };
}