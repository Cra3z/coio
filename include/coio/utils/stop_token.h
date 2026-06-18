// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <tuple>
#include <coio/utils/type_traits.h>
#include <coio/detail/elide.h>
#include <coio/detail/execution.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep

namespace coio {
    namespace detail {
        template<typename Fn>
        struct call_once {
            auto operator() () -> void {
                if (flag.exchange(false, std::memory_order_acq_rel)) {
                    std::invoke(fn);
                }
            }

            Fn fn;
            std::atomic<bool> flag{true};
        };
    }

    template<stoppable_token... StopTokens> requires (sizeof...(StopTokens) >= 2)
    class stop_combiner {
    private:
        using tlist = type_list<StopTokens...>;

    public:
        template<typename Callback>
        class callback_type {
        private:
            using cbref = std::reference_wrapper<detail::call_once<Callback>>;
            using tpl = std::tuple<stop_callback_for_t<StopTokens, cbref>...>;

        public:
            template<typename Initializer>
            callback_type(stop_combiner token, Initializer&& init) :
                cb_(std::forward<Initializer>(init)),
                inners_{[&]<std::size_t... I>(std::index_sequence<I...>) {
                    return tpl{
                        detail::elide{
                            [&]<typename TokenI>(TokenI tok_i) {
                                return stop_callback_for_t<TokenI, cbref>(tok_i, cb_);
                            },
                            std::get<I>(token.tokens_)
                        }...
                    };
                }(std::index_sequence_for<StopTokens...>{})} {}

            callback_type(const callback_type&) = delete;

            ~callback_type() = default;

            auto operator= (const callback_type&) -> callback_type& = delete;

        private:
            detail::call_once<Callback> cb_;
            tpl inners_;
        };

    public:
        stop_combiner(StopTokens... stop_tokens) noexcept : tokens_(std::move(stop_tokens)...) {}

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto stop_possible() const noexcept -> bool {
            return (any_stop_possible_)(std::index_sequence_for<StopTokens...>{});
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto stop_requested() const noexcept -> bool {
            return (any_stop_requested_)(std::index_sequence_for<StopTokens...>{});
        }

        friend auto operator== (const stop_combiner& lhs, const stop_combiner& rhs) -> bool = default;

    private:
        template<std::size_t... I>
        COIO_ALWAYS_INLINE auto any_stop_possible_(std::index_sequence<I...>) const noexcept {
            return ( ... or std::get<I>(tokens_).stop_possible() );
        }

        template<std::size_t... I>
        COIO_ALWAYS_INLINE auto any_stop_requested_(std::index_sequence<I...>) const noexcept {
            return ( ... or std::get<I>(tokens_).stop_requested() );
        }

    public:
        std::tuple<StopTokens...> tokens_;
    };


    template<stoppable_source StopSource, stoppable_token StopToken>
    class stop_propagator {
    public:
        template<typename... Args> requires std::constructible_from<StopSource, Args...>
        explicit stop_propagator(StopToken stop_token, Args&&... args) :
            stop_source_(std::forward<Args>(args)...),
            stop_callback_(
                std::move(stop_token),
                std::bind_front(&stop_propagator::stop_, std::ref(*this))
            ) {}


        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_token() const noexcept(noexcept(std::declval<StopSource&>().get_token())) {
            return stop_source_.get_token();
        }

    private:
        COIO_ALWAYS_INLINE auto stop_() -> void {
            stop_source_.request_stop();
        }

    private:
        using stop_cb_t = decltype(std::bind_front(
            &stop_propagator::stop_,
            std::ref(std::declval<stop_propagator&>())
        ));

        StopSource stop_source_;
        COIO_NO_UNIQUE_ADDRESS stop_callback_for_t<StopToken, stop_cb_t> stop_callback_;
    };

    template<stoppable_source StopSource, stoppable_token StopToken>
        requires std::same_as<std::decay_t<decltype(std::declval<StopSource>().get_token())>, StopToken>
    class stop_propagator<StopSource, StopToken> {
    public:
        explicit stop_propagator(StopToken stop_token) noexcept : stop_token_(stop_token) {}

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto get_token() const noexcept -> StopToken {
            return stop_token_;
        }

    private:
        StopToken stop_token_;
    };


    struct stop_when_t {
        template<execution::sender Sndr, stoppable_token StopToken>
        struct sender {
            using sender_concept = execution::sender_tag;

            template<execution::receiver Rcvr>
            struct state {
                using operation_state_concept = execution::operation_state_tag;
                using stop_token_t = stop_combiner<StopToken, stop_token_of_t<execution::env_of_t<Rcvr>>>;

                struct data_t {
                    explicit data_t(Rcvr rcvr, StopToken prev_token) :
                        rcvr{rcvr},
                        stop_token(std::move(prev_token), get_stop_token(execution::get_env(this->rcvr))) {}

                    Rcvr rcvr;
                    stop_token_t stop_token;
                };

                struct env {
                    COIO_ALWAYS_INLINE auto query(get_stop_token_t) const noexcept -> stop_token_t {
                        return d->stop_token;
                    }

                    template<typename Prop, typename... Args>
                        requires std::default_initializable<Prop> and (forwarding_query(Prop{}))
                            and std::invocable<Prop, execution::env_of_t<Rcvr>, Args...>
                    COIO_ALWAYS_INLINE auto query(const Prop& prop, Args&&... args) const noexcept {
                        return prop(execution::get_env(d->rcvr), std::forward<Args>(args)...);
                    }

                    data_t* d;
                };

                struct receiver {
                    using receiver_concept = execution::receiver_tag;

                    COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                        return env{d};
                    }

                    template<typename... Args>
                    COIO_ALWAYS_INLINE auto set_value(Args&&... args) && noexcept -> void {
                        execution::set_value(std::move(std::exchange(d, nullptr)->rcvr), std::forward<Args>(args)...);
                    }

                    template<typename E>
                    COIO_ALWAYS_INLINE auto set_error(E&& e) && noexcept -> void {
                        execution::set_error(std::move(std::exchange(d, nullptr)->rcvr), std::forward<E>(e));
                    }

                    COIO_ALWAYS_INLINE auto set_stopped() && noexcept -> void {
                        execution::set_stopped(std::move(std::exchange(d, nullptr)->rcvr));
                    }

                    data_t* d;
                };

                using inner_state_t = execution::connect_result_t<Sndr, receiver>;

                state(Sndr sndr, StopToken stop_token, Rcvr rcvr) :
                    data{std::move(rcvr), std::move(stop_token)},
                    inner_state(execution::connect(std::move(sndr), receiver{&data})) {}

                COIO_ALWAYS_INLINE auto start() & noexcept -> void {
                    execution::start(inner_state);
                }

                data_t data;
                inner_state_t inner_state;
            };

            template<similar_to<sender>, typename... Env>
            static consteval auto get_completion_signatures() noexcept {
                return execution::completion_signatures_of_t<Sndr, Env...>{};
            }

            template<execution::receiver Rcvr>
            COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept -> state<Rcvr> {
                return state<Rcvr>{std::move(sndr), std::move(stop_token), std::move(rcvr)};
            }

            [[nodiscard]]
            COIO_ALWAYS_INLINE auto get_env() const noexcept {
                return detail::fwd_env(execution::get_env(sndr));
            }

            StopToken stop_token;
            Sndr sndr;
        };

        template<execution::sender Sndr, stoppable_token StopToken>
        COIO_ALWAYS_INLINE COIO_STATIC_CALL_OP auto operator()(Sndr sndr, StopToken stop_token) COIO_STATIC_CALL_OP_CONST noexcept  {
            if constexpr (unstoppable_token<StopToken>) {
                return std::move(sndr);
            }
            else {
                return sender<Sndr, StopToken>{std::move(stop_token), std::move(sndr)};
            }
        }
    };

    inline constexpr stop_when_t stop_when{};
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
