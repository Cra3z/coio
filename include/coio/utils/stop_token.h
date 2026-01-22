#pragma once
#include <concepts>
#include <mutex>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include "scope_exit.h"
#include "type_traits.h"
#include "../detail/elide.h"
#include "../detail/execution.h"

namespace coio {
    namespace detail {
        template<template<typename> typename>
        struct check_type_alias_exists;

        template<typename StopToken>
        struct stop_callback_traits;

        template<typename StopToken> requires requires {
            typename check_type_alias_exists<StopToken::template callback_type>;
        }
        struct stop_callback_traits<StopToken> {
            template<typename Fn>
            using callback_type = typename StopToken::template callback_type<Fn>;
        };

        template<>
        struct stop_callback_traits<std::stop_token> {
            template<typename Fn>
            using callback_type = std::stop_callback<Fn>;
        };
    }

    template<typename StopToken, typename Callback>
    using stop_callback_for_t = typename detail::stop_callback_traits<StopToken>::template callback_type<Callback>;

    template<typename StopToken>
    concept stoppable_token = requires(const StopToken& token) {
        typename detail::check_type_alias_exists<detail::stop_callback_traits<StopToken>::template callback_type>;
        { token.stop_requested() } noexcept -> std::same_as<bool>;
        { token.stop_possible() } noexcept -> std::same_as<bool>;
        { StopToken(token) } noexcept;
    } and std::copyable<StopToken> and std::equality_comparable<StopToken>;

    template<typename Source>
    concept stoppable_source = requires(Source& src, const Source& csrc) {
        { csrc.get_token() } -> stoppable_token;
        { csrc.stop_possible() } noexcept -> std::same_as<bool>;
        { csrc.stop_requested() } noexcept -> std::same_as<bool>;
        { src.request_stop() } -> std::same_as<bool>;
    };

    template<typename Token>
    concept unstoppable_token = stoppable_token<Token> and requires(const Token tok) {
        requires std::bool_constant<not Token::stop_possible()>::value;
    };

    template<typename Env>
    using stop_token_of_t = std::decay_t<std::invoke_result_t<get_stop_token_t, Env>>;

    class never_stop_token {
    private:
        struct ignored {
            explicit ignored(never_stop_token, auto&&) noexcept {}
        };

    public:
        template<typename>
        using callback_type = ignored;

        [[nodiscard]]
        static constexpr auto stop_requested() noexcept -> bool {
            return false;
        }

        static constexpr auto stop_possible() noexcept -> bool {
            return false;
        }

        auto operator==(const never_stop_token&) const -> bool = default;
    };

    class inplace_stop_source;

    template<typename Callback>
    class inplace_stop_callback;

    class inplace_stop_token {
        friend inplace_stop_source;

        template<typename Callback>
        friend class inplace_stop_callback;

    public:
        template<typename Callback>
        using callback_type = inplace_stop_callback<Callback>;

    private:
        explicit inplace_stop_token(inplace_stop_source& src) noexcept : src_(&src) {}

    public:
        inplace_stop_token() = default;

        constexpr auto stop_requested() const noexcept -> bool;

        constexpr auto stop_possible() const noexcept -> bool {
            return src_;
        }

        auto swap(inplace_stop_token& other) noexcept -> void {
            std::swap(src_, other.src_);
        }

        friend auto swap(inplace_stop_token& lhs, inplace_stop_token& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

        auto operator== (const inplace_stop_token&) const -> bool = default;

    private:
        inplace_stop_source* src_{};
    };


    class inplace_stop_source {
        template<typename Callback>
        friend class inplace_stop_callback;
    private:
        class callback_base {
            friend inplace_stop_source;
        public:
            constexpr callback_base() = default;

            callback_base(const callback_base&) = delete;

            virtual ~callback_base() = default;

            auto operator=(const callback_base&) -> callback_base& = delete;

        private:
            virtual auto invoke_() -> void = 0;

        private:
            callback_base* next_{};
        };

    public:
        [[nodiscard]]
        auto stop_requested() const noexcept -> bool {
            return stopped_;
        }

        [[nodiscard]]
        static constexpr auto stop_possible() noexcept -> bool {
            return true;
        }

        [[nodiscard]]
        auto get_token() const noexcept -> inplace_stop_token {
            return inplace_stop_token(const_cast<inplace_stop_source&>(*this));
        }

        auto request_stop() -> bool {
            if (stopped_.exchange(true)) return false;

            std::unique_lock guard{mtx_};
            for (auto it = cbs_; it != nullptr; it = cbs_) {
                running_ = it;
                id_ = std::this_thread::get_id();
                cbs_ = it->next_;
                {
                    scope_exit _{[&guard] {
                        guard.lock();
                    }};
                    guard.unlock();
                    it->invoke_();
                }
                running_ = nullptr;
            }
            return true;
        }

        auto register_callback_(callback_base* cb) -> void {
            if (stopped_) {
                cb->invoke_();
            }
            else {
                std::scoped_lock _{mtx_};
                cb->next_ = std::exchange(cbs_, cb);
            }
        }

        auto unregister_callback_(callback_base* cb) -> void {
            std::unique_lock guard{mtx_};
            if (running_ == cb) {
                if (id_ == std::this_thread::get_id()) return;
                guard.unlock();
                while (running_ == cb) {}
                return;
            }

            for (callback_base** it{&cbs_}; *it; it = &(*it)->next_) {
                if (*it == cb) {
                    *it = cb->next_;
                    break;
                }
            }
        }

    private:
        std::atomic<bool> stopped_{false};
        std::atomic<callback_base*> running_{nullptr};
        std::thread::id id_{};
        std::mutex mtx_;
        callback_base* cbs_{nullptr};
    };


    template<typename Callback>
    class inplace_stop_callback : public inplace_stop_source::callback_base {
    public:
        using callback_type = Callback;

    public:
        template<typename Initializer>
        inplace_stop_callback(inplace_stop_token token, Initializer&& init) :
            cb_(std::forward<Initializer>(init)),
            src_(token.src_)
        {
            if (src_) src_->register_callback_(this);
        }

        inplace_stop_callback(const inplace_stop_callback&) = delete;

        ~inplace_stop_callback() override {
            if (src_) src_->unregister_callback_(this);
        }

        auto operator= (const inplace_stop_callback&) -> inplace_stop_callback& = delete;

    private:
        auto invoke_() -> void override {
            cb_();
        }

    private:
        Callback cb_;
        inplace_stop_source* src_;
    };

    constexpr auto inplace_stop_token::stop_requested() const noexcept -> bool {
        return src_ and src_->stop_requested();
    }

    template<typename Callback>
    inplace_stop_callback(inplace_stop_token, Callback) -> inplace_stop_callback<Callback>;


    namespace detail {
        template<std::invocable Fn>
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
    class union_stop_token {
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
            callback_type(union_stop_token token, Initializer&& init) :
                cb_(std::forward<Initializer>(init)),
                inners_{[&]<std::size_t... I>(std::index_sequence<I...>) {
                    return tpl{(detail::elide{
                        [&]<typename TokenI>(TokenI tok_i) {
                            return stop_callback_for_t<TokenI, cbref>(tok_i, cb_);
                        },
                        std::get<I>(token.tokens_)
                    }, ...)};
                }(std::index_sequence_for<StopTokens...>{})} {}

            callback_type(const callback_type&) = delete;

            ~callback_type() = default;

            auto operator= (const callback_type&) -> callback_type& = delete;

        private:
            detail::call_once<Callback> cb_;
            tpl inners_;
        };

    public:
        union_stop_token(StopTokens... stop_tokens) noexcept : tokens_(std::move(stop_tokens)...) {}

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto stop_possible() const noexcept -> bool {
            return (any_stop_possible_)(std::index_sequence_for<StopTokens...>{});
        }

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto stop_requested() const noexcept -> bool {
            return (any_stop_requested_)(std::index_sequence_for<StopTokens...>{});
        }

        friend auto operator== (const union_stop_token& lhs, const union_stop_token& rhs) -> bool = default;

    private:
        template<std::size_t... I>
        COIO_ALWAYS_INLINE auto any_stop_possible_(std::index_sequence<I...>) noexcept {
            return ( ... or std::get<I>(tokens_).stop_possible() );
        }

        template<std::size_t... I>
        COIO_ALWAYS_INLINE auto any_stop_requested_(std::index_sequence<I...>) noexcept {
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
}