#pragma once
#include <coio/detail/execution.h>

namespace coio {
    class async_scope {
    public:
        using token = execution::counting_scope::token;

    public:
        async_scope() = default;

        async_scope(const async_scope&) = delete;

        ~async_scope() = default;

        auto operator= (const async_scope&) -> async_scope& = delete;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto join() noexcept {
            return impl_.join();
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_token() noexcept -> token {
            return impl_.get_token();
        }

        COIO_ALWAYS_INLINE auto request_stop() noexcept -> void {
            impl_.request_stop();
        }

        COIO_ALWAYS_INLINE auto close() noexcept -> void {
            impl_.close();
        }

        COIO_ALWAYS_INLINE auto spawn(execution::sender auto sndr) noexcept -> void {
            execution::spawn(execution::upon_error(std::move(sndr), terminate_on_error), get_token());
        }

    public:
        static constexpr std::size_t max_associations = execution::counting_scope::max_associations;

    private:
        static constexpr auto terminate_on_error = [](const auto&...) noexcept {
            std::terminate();
        };

        execution::counting_scope impl_;
    };
}