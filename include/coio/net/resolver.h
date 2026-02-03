#pragma once
#include "basic.h"
#include "../generator.h"
#include "../core.h"

namespace coio {
    namespace detail {
        int ai_canonname_v() noexcept;

        int ai_passive_v() noexcept;

        int ai_numerichost_v() noexcept;

        int ai_numericserv_v() noexcept;

        int ai_v4mapped_v() noexcept;

        int ai_all_v() noexcept;

        int ai_addrconfig_v() noexcept;

        struct resolve_query_t {
            inline static const int canonical_name = ai_canonname_v();
            inline static const int passive = ai_passive_v();
            inline static const int numeric_host = ai_numerichost_v();
            inline static const int numeric_service = ai_numericserv_v();
            inline static const int v4_mapped = ai_v4mapped_v();
            inline static const int all_matching = ai_all_v();
            inline static const int address_configured = ai_addrconfig_v();

            std::string host_name;
            std::string service_name;
            int         flags{v4_mapped | address_configured};
        };

        struct resolve_result_t {
            class endpoint endpoint;
            std::string canonical_name;
        };

        auto resolve_impl(resolve_query_t query, int family, int socktype, int protocol_id) -> generator<resolve_result_t>;

        auto resolve_impl(resolve_query_t query, int sock_type, int protocol_id) -> generator<resolve_result_t>;

    }

    using detail::resolve_query_t;
    using detail::resolve_result_t;

    template<typename Protocol, scheduler Scheduler>
    class basic_resolver {
    public:
        using protocol_type = Protocol;
        using scheduler_type = Scheduler;
        using query_t = detail::resolve_query_t;
        using result_t = detail::resolve_result_t;

    public:
        explicit basic_resolver(Scheduler sched) noexcept : sched_(std::move(sched)) {}

        [[nodiscard]]
        auto get_scheduler() const noexcept -> Scheduler {
            return sched_;
        }

        /**
         * \brief resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        auto resolve(query_t query) const -> generator<result_t> {
            return detail::resolve_impl(std::move(query), protocol_type::type(), protocol_type::protocol_id());
        }

        /**
         * \brief resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        auto resolve(const protocol_type& protocol, query_t query) const -> generator<result_t> {
            return detail::resolve_impl(
                std::move(query),
                protocol.family(),
                protocol_type::type(),
                protocol_type::protocol_id()
            );
        }

        /**
         * \brief asynchronously resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        auto async_resolve(query_t query) const -> task<generator<result_t>> {
            co_await sched_.schedule();
            co_return resolve(std::move(query));
        }

        /**
         * \brief asynchronously resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        auto async_resolve(protocol_type protocol, query_t query) const -> task<generator<result_t>> {
            co_await sched_.schedule();
            co_return resolve(std::move(protocol), std::move(query));
        }

    private:
        Scheduler sched_;
    };
}