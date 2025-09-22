#pragma once
#include "../execution_context.h"
#include "internet.h"

namespace coio {
    namespace detail {
        class async_io_sender_base {
            friend io_context;
            friend io_context::impl;
        public:
            enum class category : unsigned char {
                input = 1,
                output
            };

            class pinned : public execution_context::node {
                friend io_context;
                friend io_context::impl;
            public:
                explicit pinned(const async_io_sender_base& io_sender) :
                    node(*io_sender.context_),
                    native_handle_(io_sender.native_handle_),
                    category_(io_sender.category_),
                    lazy_(io_sender.lazy_),
                    work_guard_(*io_sender.context_) {}

                auto await_suspend(std::coroutine_handle<>) -> void;

            private:
                socket_native_handle_type native_handle_ = invalid_socket_handle_value;
                category category_;
                bool lazy_; // indicate that this operation's `await_ready` always returns false
                execution_context::work_guard work_guard_;
            };

        protected:
            async_io_sender_base(
                execution_context& context, socket_native_handle_type native_handle, category category, bool lazy
            ) noexcept : context_(&context), native_handle_(native_handle), category_(category), lazy_(lazy) {}

            /// \note: after moved, this object will be in a valid but unspecified state.
            async_io_sender_base(async_io_sender_base&& other) noexcept :
                context_(std::exchange(other.context_, {})),
                native_handle_(std::exchange(other.native_handle_, invalid_socket_handle_value)),
                category_(std::exchange(other.category_, {})),
                lazy_(std::exchange(other.lazy_, {}))
            {}

            /// \note: after moved, this object will be in a valid but unspecified state.
            auto operator= (async_io_sender_base other) noexcept -> async_io_sender_base& {
                swap(other);
                return *this;
            }

            auto swap(async_io_sender_base& other) noexcept -> void {
                std::ranges::swap(context_, other.context_);
                std::ranges::swap(native_handle_, other.native_handle_);
                std::ranges::swap(category_, other.category_);
                std::ranges::swap(lazy_, other.lazy_);
            }

            friend auto swap(async_io_sender_base& lhs, async_io_sender_base& rhs) noexcept -> void {
                lhs.swap(rhs);
            }

        protected:
            execution_context* context_;
            socket_native_handle_type native_handle_ = invalid_socket_handle_value;
            category category_;
            bool lazy_; // indicate that this operation's `await_ready` always returns false
        };

        template<typename T>
        class async_io_result {
            static_assert(unqualified_object<T> and std::movable<T> and not std::is_array_v<T>);
        public:
            async_io_result() = default;

            auto value(T v) noexcept(std::is_nothrow_move_assignable_v<T>) {
                result_.template emplace<1>(std::move(v));
            }

            auto error(std::exception_ptr e) noexcept {
                COIO_ASSERT(e != nullptr);
                result_.template emplace<2>(std::move(e));
            }

            auto ready() const noexcept -> bool {
                return result_.index() > 0;
            }

            [[nodiscard]]
            auto get() -> T {
                COIO_ASSERT(result_.index() != 0);
                if (result_.index() == 1) {
                    return *std::get_if<1>(&result_);
                }
                auto exp =*std::get_if<2>(&result_);
                COIO_ASSERT(exp != nullptr);
                std::rethrow_exception(exp);
            }

        private:
            std::variant<std::monostate, T, std::exception_ptr> result_;
        };

        template<typename Customization>
        class async_io_sender : public async_io_sender_base, private Customization {
        public:
            class awaiter : pinned {
                friend async_io_sender;
            private:
                awaiter(async_io_sender&& impl) noexcept : pinned(impl), impl_(std::move(impl)) {}

            public:
                auto await_ready() noexcept -> bool {
                    return impl_.try_perform();
                }

                using pinned::await_suspend;

                auto await_resume() -> typename Customization::result_type {
                    return impl_.on_completion();
                }

            private:
                async_io_sender impl_;
            };

        public:
            async_io_sender(
                io_context& context,
                socket_native_handle_type native_handle,
                Customization extra
            ) noexcept :
                async_io_sender_base(context, native_handle, Customization::category, Customization::is_lazy),
                Customization(std::exchange(extra, {})) {}

            async_io_sender(const async_io_sender&) = delete;

            async_io_sender(async_io_sender&& other) noexcept :
                async_io_sender_base(static_cast<async_io_sender_base&&>(other)),
                Customization(std::exchange(static_cast<Customization&>(other), {}))
            {}

            auto operator= (async_io_sender other) noexcept -> async_io_sender& {
                this->swap(other);
                return *this;
            }

            auto operator co_await() && noexcept -> awaiter {
                return {std::move(*this)};
            }

            auto swap(async_io_sender& other) noexcept -> void {
                std::ranges::swap(static_cast<async_io_sender_base&>(*this), static_cast<async_io_sender_base&>(other));
                std::ranges::swap(static_cast<Customization&>(*this), static_cast<Customization&>(other));
            }

            friend auto swap(async_io_sender& lhs, async_io_sender& rhs) noexcept -> void {
                lhs.swap(rhs);
            }

        private:
            auto try_perform() noexcept -> bool = delete; // NOLINT(*-use-equals-delete)

            auto on_completion() -> typename Customization::result_type = delete; // NOLINT(*-use-equals-delete)
        };

        struct async_receive {
            using result_type = std::size_t;
            static constexpr bool is_lazy = false;
            static constexpr async_io_sender_base::category category = async_io_sender_base::category::input;

            std::span<std::byte> buffer_;
            bool zero_as_eof_;
            async_io_result<std::size_t> result_;
        };

        struct async_send {
            using result_type = std::size_t;
            static constexpr bool is_lazy = false;
            static constexpr async_io_sender_base::category category = async_io_sender_base::category::output;

            std::span<const std::byte> buffer_;
            async_io_result<std::size_t> result_;
        };

        struct async_receive_from {
            using result_type = std::size_t;
            static constexpr bool is_lazy = false;
            static constexpr async_io_sender_base::category category = async_io_sender_base::category::input;

            std::span<std::byte> buffer_;
            endpoint src_;
            bool zero_as_eof_;
            async_io_result<std::size_t> result_;
        };

        struct async_send_to {
            using result_type = std::size_t;
            static constexpr bool is_lazy = false;
            static constexpr async_io_sender_base::category category = async_io_sender_base::category::output;

            std::span<const std::byte> buffer_;
            endpoint dest_;
            async_io_result<std::size_t> result_;
        };

        struct async_accept {
            using result_type = socket_native_handle_type;
            static constexpr bool is_lazy = true;
            static constexpr async_io_sender_base::category category = async_io_sender_base::category::input;

            async_io_result<socket_native_handle_type> result_;
        };

        struct async_connect {
            using result_type = void;
            static constexpr bool is_lazy = true;
            static constexpr async_io_sender_base::category category = async_io_sender_base::category::output;

            endpoint dest_;
        };

        template<>
        auto async_io_sender<async_receive>::try_perform() noexcept -> bool;

        template<>
        auto async_io_sender<async_receive>::on_completion() -> result_type;


        template<>
        auto async_io_sender<async_send>::try_perform() noexcept -> bool;

        template<>
        auto async_io_sender<async_send>::on_completion() -> result_type;


        template<>
        auto async_io_sender<async_receive_from>::try_perform() noexcept -> bool;

        template<>
        auto async_io_sender<async_receive_from>::on_completion() -> result_type;


        template<>
        auto async_io_sender<async_send_to>::try_perform() noexcept -> bool;

        template<>
        auto async_io_sender<async_send_to>::on_completion() -> result_type;


        template<>
        auto async_io_sender<async_accept>::try_perform() noexcept -> bool;

        template<>
        auto async_io_sender<async_accept>::on_completion() -> result_type;


        template<>
        auto async_io_sender<async_connect>::try_perform() noexcept -> bool;

        template<>
        auto async_io_sender<async_connect>::on_completion() -> result_type;
    }

    using async_receive_t = detail::async_io_sender<detail::async_receive>;

    using async_send_t = detail::async_io_sender<detail::async_send>;

    using async_receive_from_t = detail::async_io_sender<detail::async_receive_from>;

    using async_send_to_t = detail::async_io_sender<detail::async_send_to>;

    using async_accept_t = detail::async_io_sender<detail::async_accept>;

    using async_connect_t = detail::async_io_sender<detail::async_connect>;
}