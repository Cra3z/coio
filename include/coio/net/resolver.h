#pragma once
#include <string>
#include <coio/net/basic.h>
#include <coio/generator.h>
#include <coio/core.h>

namespace coio {
    namespace detail {
        auto ai_canonname_v() noexcept -> int;

        auto ai_passive_v() noexcept -> int;

        auto ai_numerichost_v() noexcept -> int;

        auto ai_numericserv_v() noexcept -> int;

        auto ai_v4mapped_v() noexcept -> int;

        auto ai_all_v() noexcept -> int;

        auto ai_addrconfig_v() noexcept -> int;

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
        COIO_ALWAYS_INLINE auto get_scheduler() const noexcept -> Scheduler {
            return sched_;
        }

        /**
         * \brief resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE static auto resolve(query_t query) -> generator<result_t> {
            return detail::resolve_impl(std::move(query), protocol_type::type(), protocol_type::protocol_id());
        }

        /**
         * \brief resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE static auto resolve(const protocol_type& protocol, query_t query) -> generator<result_t> {
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
        COIO_ALWAYS_INLINE auto async_resolve(query_t query) const {
            return execution::schedule(sched_) | execution::then([=] {
                return resolve(std::move(query));
            });
        }

        /**
         * \brief asynchronously resolve a query into a sequence of endpoint entries.
         */
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto async_resolve(protocol_type protocol, query_t query) const {
            return execution::schedule(sched_) | execution::then([=] {
                 return resolve(std::move(protocol), std::move(query));
             });
        }

    private:
        Scheduler sched_;
    };
}