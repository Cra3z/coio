// implement https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3149r11.html
// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <mutex>
#include "atomutex.h"
#include "stop_token.h"
#include "utility.h"

namespace coio {
    class async_scope {
    public:
        class association {
        public:
            association() = default;

            explicit association(async_scope& scope) noexcept : scope(&scope) {}

            association(const association&) = delete;

            association(association&& other) noexcept : scope(std::exchange(other.scope, nullptr)) {}

            auto operator= (association other) noexcept -> association& {
                std::swap(scope, other.scope);
                return *this;
            }

            ~association() {
                if (scope) scope->disassociate();
            }

            explicit operator bool() const noexcept {
                return scope != nullptr;
            }

            auto try_associate() const noexcept -> association {
                if (scope) return scope->try_associate();
                return {};
            }

        private:
            async_scope* scope = nullptr;
        };

        class token {
        public:
            explicit token(async_scope& scope) : scope(&scope) {}

            COIO_ALWAYS_INLINE auto try_associate() const noexcept -> association {
                return scope->try_associate();
            }

            template<execution::sender Sender>
            COIO_ALWAYS_INLINE auto wrap(Sender&& sender) const noexcept {
                return stop_when(
                    ::std::forward<Sender>(sender),
                    scope->stop_source_.get_token()
                );
            }

        private:
            async_scope* scope;
        };

    private:
        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        struct state_base {
            using operation_state_concept = execution::operation_state_t;
            using finish_fn_t = void(*)(state_base*) noexcept;

            state_base(finish_fn_t finish) noexcept : finish(finish) {}

            state_base(const state_base&) = delete;

            auto operator= (const state_base&) -> state_base& = delete;

            const finish_fn_t finish;
        };

        struct state_node : state_base {
            using state_base::state_base;
            state_node* next = nullptr;
        };

        struct join_sender {
            using sender_concept = execution::sender_t;
            using completion_signatures = execution::completion_signatures<execution::set_value_t()>;
            // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
            template<typename Rcvr>
            struct state : state_node {
                using schedule_sender_t = decltype(execution::schedule(detail::query_or(execution::get_scheduler, execution::get_env(std::declval<Rcvr>()), execution::inline_scheduler{})));
                using inner_state = execution::connect_result_t<schedule_sender_t, Rcvr>;
                explicit state(async_scope& scope, Rcvr rcvr) noexcept :
                    state_node(&finish),
                    scope(scope),
                    inner(execution::connect(
                        execution::schedule(detail::query_or(execution::get_scheduler, execution::get_env(rcvr), execution::inline_scheduler{})),
                        std::move(rcvr)
                    )) {}

                COIO_ALWAYS_INLINE auto start() & noexcept -> void {
                    scope.add_listener(this);
                }

                static auto finish(state_base* self) noexcept -> void {
                    execution::start(static_cast<state*>(self)->inner);
                }

                async_scope& scope;
                inner_state inner;
            };

            join_sender(async_scope& scope) noexcept : scope_(&scope) {}

            join_sender(const join_sender&) = delete;

            join_sender(join_sender&& other) noexcept : scope_(std::exchange(other.scope_, {})) {}

            ~join_sender() = default;

            auto operator= (join_sender other) noexcept -> join_sender& {
                std::swap(scope_, other.scope_);
                return *this;
            }

            template<execution::receiver Rcvr>
            COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                COIO_ASSERT(scope_ != nullptr);
                return state<Rcvr>{*std::exchange(scope_, {}), std::move(rcvr)};
            }
        private:
            async_scope* scope_;
        };

    private:
        enum class state : unsigned char {
            unused,
            open,
            open_and_joining,
            closed,
            closed_and_joining,
            unused_and_closed,
            joined
        };

    public:
        async_scope() = default;

        async_scope(const async_scope&) = delete;

        ~async_scope() {
            std::scoped_lock _{mutex_};
            switch (state_) {
            using enum state;
            case unused:
            case unused_and_closed:
            case joined:
                break;
            default:
                std::terminate();
            }
        }

        auto operator= (const async_scope&) -> async_scope& = delete;

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto join() noexcept {
            return join_sender{*this};
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_token() noexcept -> token {
            return token{*this};
        }

        COIO_ALWAYS_INLINE auto request_stop() noexcept -> void {
            stop_source_.request_stop();
        }

        COIO_ALWAYS_INLINE auto close() noexcept -> void {
            std::scoped_lock _{mutex_};
            switch (state_) {
            using enum state;
            case unused:
                state_ = unused_and_closed;
                break;
            case open:
                state_ = closed;
                break;
            case open_and_joining:
                state_ = closed_and_joining;
                break;
            default:
                break;
            }
        }

        COIO_ALWAYS_INLINE auto spawn(execution::sender auto sndr) noexcept -> void {
            execution::spawn(std::move(sndr) | execution::let_error(terminate_on_error), get_token());
        }

    private:
        COIO_ALWAYS_INLINE auto add_listener(state_node* node) -> void {
            std::unique_lock lck{mutex_};
            switch (state_) {
            using enum state;
            case unused:
            case unused_and_closed:
            case joined:
                state_ = joined;
                lck.unlock();
                node->finish(node);
                return;
            case open:
                state_ = open_and_joining;
                break;
            case open_and_joining:
                break;
            case closed:
                state_ = closed_and_joining;
                break;
            case closed_and_joining:
                break;
            default:
                unreachable();
            }
            node->next = std::exchange(head_, node);
        }

        COIO_ALWAYS_INLINE auto try_associate() noexcept -> association {
            std::scoped_lock _{mutex_};
            switch (state_) {
            case state::unused:
                state_ = state::open;
                [[fallthrough]];
            case state::open:
            case state::open_and_joining:
                ++count_;
                return association{*this};
            default:
                return {};
            }
        }

        COIO_ALWAYS_INLINE auto disassociate() noexcept -> void {
            {
                std::scoped_lock _{mutex_};
                if (--count_ > 0) return;
                state_ = state::joined;
            }
            wakeup_all();
        }

        COIO_ALWAYS_INLINE auto wakeup_all() noexcept -> void {
            state_node* node = nullptr;
            {
                std::scoped_lock _{mutex_};
                node = std::exchange(head_, nullptr);
            }
            while (node) {
                const auto next = node->next;
                node->finish(node);
                node = next;
            }
        }

    private:
        static constexpr auto terminate_on_error = [](const auto&...) noexcept -> std::invoke_result_t<execution::just_t> {
            std::terminate();
        };
        atomutex mutex_;
        std::size_t count_{};
        state state_{state::unused};
        state_node* head_{};
        inplace_stop_source stop_source_;
    };
}