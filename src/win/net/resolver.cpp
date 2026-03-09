#include <coio/net/resolver.h>
#include <coio/utils/scope_exit.h>
#include "../common.h"

namespace coio {
    namespace detail {
        int ai_canonname_v()     noexcept { return AI_CANONNAME;        }
        int ai_passive_v()       noexcept { return AI_PASSIVE;          }
        int ai_numerichost_v()   noexcept { return AI_NUMERICHOST;      }
        int ai_numericserv_v()   noexcept { return AI_NUMERICSERV;      }
        int ai_v4mapped_v()      noexcept { return AI_V4MAPPED;         }
        int ai_all_v()           noexcept { return AI_ALL;              }
        int ai_addrconfig_v()    noexcept { return AI_ADDRCONFIG;       }

        auto resolve_impl(resolve_query_t query, int socktype, int protocol_id)
            -> generator<resolve_result_t>
        {
            return resolve_impl(std::move(query), AF_UNSPEC, socktype, protocol_id);
        }

        auto resolve_impl(resolve_query_t query, int family, int socktype, int protocol_id)
            -> generator<resolve_result_t>
        {
            ADDRINFOA hints{};
            hints.ai_flags    = query.flags;
            hints.ai_family   = family;
            hints.ai_socktype = socktype;
            hints.ai_protocol = protocol_id;

            ADDRINFOA* ai_head = nullptr;
            const int ec = ::getaddrinfo(
                query.host_name.empty()    ? nullptr : query.host_name.c_str(),
                query.service_name.empty() ? nullptr : query.service_name.c_str(),
                &hints, &ai_head);
            if (ec != 0) throw std::system_error(ec, error::gai_category());

            scope_exit _{[ai_head]() noexcept { ::freeaddrinfo(ai_head); }};

            for (auto* ai = ai_head; ai != nullptr; ai = ai->ai_next) {
                co_yield {
                    sockaddr_to_endpoint(ai->ai_addr),
                    ai->ai_canonname ? ai->ai_canonname : ""
                };
            }
        }
    } // namespace detail
} // namespace coio
