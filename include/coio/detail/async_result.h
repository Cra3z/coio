#pragma once
#include <type_traits>
#include <variant>
#include "execution.h"
#include "coio/utils/utility.h"

namespace coio::detail {
    template<typename T, typename E>
    class async_result {
    public:
        using sender_concept = execution::sender_t;
        using receiver_concept = execution::receiver_t;
        using value_type = T;
        using error_type = E;
        using completion_signaturs = execution::completion_signatures<
            detail::set_value_t<value_type>,
            execution::set_error_t(error_type),
            execution::set_stopped_t()
        >;

    public:
        async_result() = default;

        template<typename... Args> requires (std::is_void_v<T> and sizeof...(Args) == 0) or (std::constructible_from<T, Args...>)
        auto set_value(Args&&... args) noexcept(std::is_nothrow_move_assignable_v<T>) {
            result_.template emplace<1>(std::forward<Args>(args)...);
        }

        auto set_error(error_type e) noexcept -> void {
            result_.template emplace<2>(std::move(e));
        }

        auto set_stopped() noexcept -> void {
            result_.template emplace<0>();
        }

        template<execution::receiver_of<completion_signaturs> Rcvr>
        auto forward_to(Rcvr&& rcvr) noexcept -> void {
            switch (result_.index()) {
            case 0: {
                execution::set_stopped(std::forward<Rcvr>(rcvr));
                break;
            }
            case 1: {
                if constexpr (std::is_void_v<T>) {
                    execution::set_value(std::forward<Rcvr>(rcvr));
                }
                else {
                    execution::set_value(std::forward<Rcvr>(rcvr), value_type(std::get<1>(result_)));
                }
                break;
            }
            case 2: {
                execution::set_error(std::forward<Rcvr>(rcvr), std::move(std::get<2>(result_)));
                break;
            }
            default: unreachable();
            }
        }

        template<execution::receiver_of<completion_signaturs> Rcvr>
        auto connect(Rcvr rcvr) && noexcept {
            struct state {
                auto start() & noexcept -> void {
                    self_.forward_to(std::move(rcvr_));
                }

                async_result self_;
                Rcvr rcvr_;
            };
            return state{std::move(*this), std::move(rcvr)};
        }

    private:
        std::variant<
            std::monostate,
            std::conditional_t<std::is_void_v<value_type>, std::monostate, value_type>,
            error_type
        > result_;
    };
}
