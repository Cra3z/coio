#pragma once
#include <variant>
#include <coio/detail/concepts.h>
#include <coio/detail/execution.h>

namespace coio {
    template<typename... Sndrs>
    class variant_sender {
    private:
        using variant_t = std::variant<Sndrs...>;

        template<typename Rcvr>
        struct state {
            using operation_state_concept = execution::operation_state_tag;

            state(auto sndr, Rcvr rcvr) : state_(execution::connect(std::move(sndr), std::move(rcvr))) {}

            state(const state&) = delete;

            auto operator= (const state&) -> state& = delete;

            COIO_ALWAYS_INLINE auto start() & noexcept -> void {
                std::visit([](auto& state) noexcept {
                    execution::start(state);
                }, state_);
            }

            std::variant<execution::connect_result_t<Sndrs, Rcvr>...> state_;
        };

    public:
        using sender_concept = execution::sender_tag;

    public:
        template<typename Expr> requires different_from<std::decay_t<Expr>, variant_sender> and std::convertible_to<Expr, variant_t>
        variant_sender(Expr&& sndr) noexcept(std::is_nothrow_convertible_v<Expr, variant_t>) : sndr_(std::forward<Expr>(sndr)) {}

        template<typename Rcvr>
        COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
            return std::visit(
                [&rcvr](auto sndr) noexcept {
                    return state<Rcvr>{std::move(sndr), std::move(rcvr)};
                },
                std::move(sndr_)
            );
        }

        template<similar_to<variant_sender>, typename... Env>
        static consteval auto get_completion_signatures() noexcept {
            return detail::merge_completion_signatures_t<execution::completion_signatures_of_t<Sndrs, Env...>...>{};
        }

    private:
        variant_t sndr_;
    };
}
