#pragma once
#include <tuple>
#include <type_traits>
#include <variant>
#include <coio/detail/execution.h>
#include <coio/utils/utility.h>

namespace coio::detail {
    template<typename, typename>
    class async_result;

    template<typename... Values, typename Error>
    class async_result<execution::set_value_t(Values...), execution::set_error_t(Error)> {
    public:
        using sender_concept = execution::sender_t;
        using receiver_concept = execution::receiver_t;
        using completion_signatures = execution::completion_signatures<
            execution::set_value_t(Values...),
            execution::set_error_t(Error),
            execution::set_stopped_t()
        >;

    public:
        async_result() = default;

        COIO_ALWAYS_INLINE auto set_value(Values... values) noexcept {
            COIO_ASSERT(result_.index() == 0);
            result_.template emplace<1>(std::forward<Values>(values)...);
        }

        COIO_ALWAYS_INLINE auto set_error(Error e) noexcept -> void {
            COIO_ASSERT(result_.index() == 0);
            result_.template emplace<2>(std::forward<Error>(e));
        }

        COIO_ALWAYS_INLINE auto set_stopped() noexcept -> void {
            COIO_ASSERT(result_.index() == 0);
            result_.template emplace<0>();
        }

        template<similar_to<async_result>, typename...>
        static consteval auto get_completion_signatures() noexcept -> completion_signatures {
            return {};
        }

        template<execution::receiver_of<completion_signatures> Rcvr>
        COIO_ALWAYS_INLINE auto forward_to(Rcvr&& rcvr) noexcept -> void {
            switch (result_.index()) {
            case 0: {
                execution::set_stopped(std::forward<Rcvr>(rcvr));
                break;
            }
            case 1: {
                std::apply(std::bind_front(execution::set_value, std::forward<Rcvr>(rcvr)), std::move(std::get<1>(result_)));
                break;
            }
            case 2: {
                execution::set_error(std::forward<Rcvr>(rcvr), std::move(std::get<2>(result_)));
                break;
            }
            default: unreachable();
            }
        }

        template<execution::receiver_of<completion_signatures> Rcvr>
        COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
            struct state {
                using operation_state_concept = execution::operation_state_t;

                COIO_ALWAYS_INLINE auto start() & noexcept -> void {
                    self_.forward_to(std::move(rcvr_));
                }

                async_result self_;
                Rcvr rcvr_;
            };
            return state{std::move(*this), std::move(rcvr)};
        }

    private:
        std::variant<std::monostate, std::tuple<Values...>, Error> result_;
    };
}
