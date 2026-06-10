#pragma once
#include <tuple>
#include <coio/detail/execution.h>
#include <coio/utils/stop_token.h>
#include <coio/utils/utility.h>

namespace coio {
    struct when_any_t {
        template<typename>
        struct env;

        template<typename>
        struct state_base;

        template<execution::receiver, typename, typename>
        struct state_value;

        template<execution::receiver, typename, typename>
        struct receiver;

        template<typename, execution::receiver, typename, typename, execution::sender...>
        struct state;

        template<std::size_t... I, execution::receiver Receiver, typename Value, typename Error, execution::sender... Sender>
        struct state<std::index_sequence<I...>, Receiver, Value, Error, Sender...>;

        template<execution::sender...>
        struct sender;

        template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
        COIO_ALWAYS_INLINE COIO_STATIC_CALL_OP auto operator()(Sender&&... sndr) COIO_STATIC_CALL_OP_CONST -> sender<Sender...> {
            return {{std::forward<Sender>(sndr)...}};
        }
    };

    template<typename Receiver>
    struct when_any_t::env {
        COIO_ALWAYS_INLINE auto query(get_stop_token_t) const noexcept -> inplace_stop_token {
            return this->state->source.get_token();
        }

        template<typename Prop, typename... Args>
            requires std::default_initializable<Prop> and
                (forwarding_query(Prop{})) and
                std::invocable<Prop, execution::env_of_t<Receiver>, Args...>
        COIO_ALWAYS_INLINE auto query(const Prop& prop, Args&&... args) const noexcept {
            return prop(execution::get_env(state->receiver), std::forward<Args>(args)...);
        }

        state_base<Receiver>* state;
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<typename Receiver>
    struct when_any_t::state_base {
        template<typename Rcvr>
        state_base(std::size_t total, Rcvr&& rcvr) : total(total), receiver(std::forward<Rcvr>(rcvr)) {}

        state_base(const state_base&) = delete;

        ~state_base() = default;

        auto operator= (const state_base&) -> state_base& = delete;

        virtual auto finish() -> void = 0;

        COIO_ALWAYS_INLINE auto report() -> bool {
            if (done_count++ == 0) {
                source.request_stop();
                return true;
            }
            return false;
        }

        COIO_ALWAYS_INLINE auto arrive() -> void {
            if (++this->ready_count == this->total) {
                this->finish();
            }
        }

        std::size_t total{};
        Receiver receiver{};
        std::atomic<std::size_t> done_count{};
        std::atomic<std::size_t> ready_count{};
        inplace_stop_source source{};
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<execution::receiver Receiver, typename Value, typename Error>
    struct when_any_t::state_value : state_base<Receiver> {
        template<typename Rcvr>
        state_value(std::size_t total, Rcvr&& rcvr) : state_base<Receiver>{total, std::forward<Rcvr>(rcvr)} {}

        auto finish() -> void override {
            switch (result.index()) {
            case 0: {
                execution::set_stopped(std::move(this->receiver));
                break;
            }
            case 1: {
                if constexpr (specialization_of<std::remove_cvref_t<decltype(std::get<1>(result))>, std::variant>) {
                    std::visit(
                        [this](auto tpl) {
                            std::apply(std::bind_front(execution::set_value, std::move(this->receiver)), std::move(tpl));
                        },
                        std::move(std::get<1>(result))
                    );
                }
                else { // no value
                    unreachable();
                }
                break;
            }
            case 2: {
                if constexpr (specialization_of<std::remove_cvref_t<decltype(std::get<2>(result))>, std::variant>) {
                    std::visit(
                        std::bind_front(execution::set_error, std::move(this->receiver)),
                        std::move(std::get<2>(result))
                    );
                }
                else { // no error
                    unreachable();
                }
                break;
            }
            default: unreachable();
            }
        }

        std::variant<std::monostate, Value, Error> result;
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<execution::receiver Receiver, typename Value, typename Error>
    struct when_any_t::receiver {
        using receiver_concept = execution::receiver_tag;

        COIO_ALWAYS_INLINE auto get_env() const noexcept -> env<Receiver> {
            return {state};
        }

        template<typename... Args>
        COIO_ALWAYS_INLINE auto set_value(Args&&... args) && noexcept -> void {
            if (state->report()) {
                state->result.template emplace<1>(
                    std::in_place_type<std::tuple<std::decay_t<Args>...>>,
                    std::forward<Args>(args)...
                );
            }
            state->arrive();
        }

        template<typename E>
        COIO_ALWAYS_INLINE auto set_error(E&& error) && noexcept -> void {
            if (state->report()) {
                state->result.template emplace<2>(std::forward<E>(error));
            }
            state->arrive();
        }

        COIO_ALWAYS_INLINE auto set_stopped() && noexcept -> void {
            state->report();
            state->arrive();
        }

        state_value<Receiver, Value, Error>* state;
    };

    // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
    template<std::size_t... I, execution::receiver Receiver, typename Value, typename Error, execution::sender... Sender>
    struct when_any_t::state<std::index_sequence<I...>, Receiver, Value, Error, Sender...> : state_value<Receiver, Value, Error> {
        using operation_state_concept = execution::operation_state_tag;
        using base = state_value<Receiver, Value, Error>;
        using value_type = Value;
        using error_type = Error;
        using receiver_type = receiver<Receiver, value_type, error_type>;
        using states_type = std::tuple<execution::connect_result_t<Sender, receiver_type>...>;

        template<typename Rcvr, typename Sndrs>
        state(Rcvr&& rcvr, Sndrs&& when_any_sndrs) :
            base(sizeof...(Sender), std::forward<Rcvr>(rcvr)),
            states{
                detail::elide{
                    execution::connect,
                    std::get<I>(std::forward<Sndrs>(when_any_sndrs)),
                    receiver_type{this}
                }...
            } {}

        state(state&&) = delete;

        COIO_ALWAYS_INLINE auto start() & noexcept -> void {
            (..., execution::start(std::get<I>(this->states)));
        }

        states_type states;
    };

    template<execution::sender... Sender>
    struct when_any_t::sender {
        using sender_concept = execution::sender_tag;

        template<execution::receiver Receiver>
        COIO_ALWAYS_INLINE auto connect(Receiver&& receiver) && -> state<
            std::index_sequence_for<Sender...>,
            std::remove_cvref_t<Receiver>,
            execution::value_types_of_t<sender, execution::env_of_t<Receiver>>,
            execution::error_types_of_t<sender, execution::env_of_t<Receiver>>,
            Sender...
        > {
            return {std::forward<Receiver>(receiver), std::move(senders)};
        }

        template<similar_to<sender>, typename... Env> requires requires {
            typename std::void_t<execution::completion_signatures_of_t<Sender, Env...>...>;
        }
        static consteval auto get_completion_signatures() noexcept {
            return detail::merge_completion_signatures_t<execution::completion_signatures_of_t<Sender, Env...>...>{};
        }

        std::tuple<std::remove_cvref_t<Sender>...> senders;
    };


    struct when_any_with_variant_t {
        template<execution::sender... Sender> requires (sizeof...(Sender) > 0)
        COIO_ALWAYS_INLINE COIO_STATIC_CALL_OP auto operator()(Sender&&... sndr) COIO_STATIC_CALL_OP_CONST {
            return execution::into_variant(when_any_t{}(std::forward<Sender>(sndr)...));
        }
    };

    inline constexpr when_any_t when_any{};
    inline constexpr when_any_with_variant_t when_any_with_variant{};
}
