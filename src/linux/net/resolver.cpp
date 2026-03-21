#include <coio/net/resolver.h>
#include <coio/utils/scope_exit.h>
#include <netdb.h>
#include "../common.h"

namespace coio {
    namespace detail {
        auto ai_canonname_v() noexcept -> int {
            return AI_CANONNAME;
        }

        auto ai_passive_v() noexcept -> int {
            return AI_PASSIVE;
        }

        auto ai_numerichost_v() noexcept -> int {
            return AI_NUMERICHOST;
        }

        auto ai_numericserv_v() noexcept -> int {
            return AI_NUMERICSERV;
        }

        auto ai_v4mapped_v() noexcept -> int {
            return AI_V4MAPPED;
        }

        auto ai_all_v() noexcept -> int {
            return AI_ALL;
        }

        auto ai_addrconfig_v() noexcept -> int {
            return AI_ADDRCONFIG;
        }

        auto resolve_impl(resolve_query_t query, int socktype, int protocol_id) -> generator<resolve_result_t> {
            return resolve_impl(std::move(query), AF_UNSPEC, socktype, protocol_id);
        }

        auto resolve_impl(resolve_query_t query, int family, int socktype, int protocol_id) -> generator<resolve_result_t> {
            ::addrinfo hints{
                .ai_flags = query.flags,
                .ai_family = family,
                .ai_socktype = socktype,
                .ai_protocol = protocol_id,
            };
            ::addrinfo* ai_head = nullptr;
            if (int ec = ::getaddrinfo(
                query.host_name.empty() ? nullptr : query.host_name.c_str(),
                query.service_name.empty() ? nullptr : query.service_name.c_str(),
                &hints, &ai_head
            )) throw std::system_error(ec, error::gai_category());
            scope_exit _{[ai_head]() noexcept {
                ::freeaddrinfo(ai_head);
            }};
            for (auto ai_node = ai_head; ai_node != nullptr; ai_node = ai_node->ai_next) {
                co_yield {sockaddr_to_endpoint(ai_node->ai_addr), ai_node->ai_canonname ? ai_node->ai_canonname : ""};
            }
        }
    }

    namespace error {
        namespace {
            struct gai_category_t : std::error_category {
                auto name() const noexcept -> const char* override {
                    return "coio.error.getaddrinfo";
                }

                auto message(int ec) const -> std::string override {
                    return ::gai_strerror(ec);
                }
            };
        }

        auto gai_category() noexcept -> const std::error_category& {
            static gai_category_t instance;
            return instance;
        }
    }
}
