#pragma once
#include <csignal> // IWYU pragma: keep
#include <set>
#include <utility>
#include "../detail/execution.h"
#include "../detail/intrusive_stack.h"

namespace coio {
    namespace detail {
        class signal_state;
    }

    class signal_set {
        friend detail::signal_state;
    private:
        class node {
            friend signal_set;
            friend detail::signal_state;
        public:
            using operation_state_concept = execution::operation_state_t;
            using finish_fn_t = void(*)(node*, int) noexcept;

        public:
            explicit node(signal_set& owner, finish_fn_t finish) noexcept : owner_(owner), finish_(finish) {};

            node(const node&) = delete;

            auto operator= (const node&) -> node& = delete;

            auto start() & noexcept -> void {
                owner_.listeners_.push(*this);
            }

        private:
            signal_set& owner_;
            const finish_fn_t finish_;
            node* next_ = nullptr;
        };

        template<typename Rcvr>
        struct op_state : node {
            op_state(signal_set& owner, Rcvr rcvr) noexcept : node(owner, &finish), rcvr(std::move(rcvr)) {}

            static auto finish(node* self, int result) noexcept -> void {
                auto& rcvr = static_cast<op_state*>(self)->rcvr;
                if (result < 0) [[unlikely]] {
                    std::error_code ec{-result, std::system_category()};
                    if (ec == std::errc::operation_canceled) {
                        execution::set_stopped(std::move(rcvr));
                        return;
                    }
                    execution::set_error(std::move(rcvr), ec);
                    return;
                }
                execution::set_value(std::move(rcvr), result);
            }

            Rcvr rcvr;
        };

    public:
        struct wait_sender {
            using sender_concept = execution::sender_t;
            using completion_signatures = execution::completion_signatures<
                execution::set_value_t(int),
                execution::set_error_t(std::error_code),
                execution::set_stopped_t()
            >;

            template<execution::receiver_of<completion_signatures> Rcvr>
            COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                COIO_ASSERT(owner != nullptr);
                return op_state<Rcvr>{*std::exchange(owner, {}), std::move(rcvr)};
            }

            signal_set* owner = nullptr;
        };

    public:
        signal_set() = default;

        explicit signal_set(std::initializer_list<int> signal_numbers) {
            for (int signum : signal_numbers) add(signum);
        }

        signal_set(const signal_set&) = delete;

        ~signal_set();

        auto operator= (const signal_set&) -> signal_set& = delete;

        [[nodiscard]]
        auto async_wait() noexcept -> wait_sender {
            return {this};
        }

        auto add(int signal_number) -> void;

        auto remove(int signal_number) -> void;

        auto cancel() -> void;

        auto clear() -> void;

    private:
        std::set<int> signal_numbers_;
        detail::intrusive_stack<node> listeners_{&node::next_};
    };
}