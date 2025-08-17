#pragma once
#include <coroutine>
#include "task.h"
#include "detail/exec.h"

namespace coio {
    template<typename Scheduler>
    concept scheduler =
        std::derived_from<typename std::remove_cvref_t<Scheduler>::scheduler_concept, detail::scheduler_tag> and
        std::copy_constructible<std::remove_cvref_t<Scheduler>> and
        std::equality_comparable<std::remove_cvref_t<Scheduler>> and
        requires {
            { std::declval<Scheduler&>().schedule() } -> awaitable;
            { std::declval<Scheduler>().schedule() } -> awaitable;
        };

    namespace detail {
        struct schedule_fn {
            template<scheduler Scheduler>
            COIO_STATIC_CALL_OP auto operator()(Scheduler&& sched) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(std::forward<Scheduler>(sched).schedule())) {
                return std::forward<Scheduler>(sched).schedule();
            }
        };

        struct starts_on_fn {
            template<scheduler Scheduler, awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                Scheduler sched, Awaitable awt
            ) COIO_STATIC_CALL_OP_CONST -> task<awaitable_await_result_t<Awaitable>> {
                return starts_on_fn{}(std::allocator_arg, std::allocator<void>{}, std::move(sched), std::move(awt));
            }

            template<typename Alloc, scheduler Scheduler, awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                std::allocator_arg_t, const Alloc&, Scheduler&& sched, Awaitable awt
            )COIO_STATIC_CALL_OP_CONST -> task<awaitable_await_result_t<Awaitable>, Alloc> {
                co_await std::forward<Scheduler>(sched).schedule();
                co_return co_await std::move(awt);
            }
        };

        struct continues_on_fn {
            template<scheduler Scheduler, awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                Awaitable awt, Scheduler sched
            ) COIO_STATIC_CALL_OP_CONST -> task<awaitable_await_result_t<Awaitable>> {
                return continues_on_fn{}(std::allocator_arg, std::allocator<void>{}, std::move(awt), std::move(sched));
            }

            template<typename Alloc, scheduler Scheduler, awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                std::allocator_arg_t, const Alloc&, Awaitable awt, Scheduler sched
            )COIO_STATIC_CALL_OP_CONST -> task<awaitable_await_result_t<Awaitable>, Alloc> {
                bool scheduled = false; // determine whether caught exception is thrown from `co_await sched.schdule()` or `co_await std::move(awt)`
                std::exception_ptr ex;
                try {
                    auto&& awaiter = detail::get_awaiter(std::move(awt));
                    if constexpr (std::is_void_v<detail::awaitable_await_result_t<Awaitable>>) {
                        co_await static_cast<decltype(awaiter)>(awaiter); // may throw
                        scheduled = true;
                        co_await sched.schedule();
                        co_return;
                    }
                    else {
                        auto&& result = co_await static_cast<decltype(awaiter)>(awaiter); // may throw
                        scheduled = true;
                        co_await sched.schedule();
                        co_return static_cast<decltype(result)>(result);
                    }
                }
                catch (...) {
                    ex = std::current_exception();
                }

                if (not scheduled) {
                    co_await sched.schedule();
                }

                std::rethrow_exception(ex);
            }
        };

        struct on_fn {
            template<scheduler Scheduler, awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                Scheduler sched, Awaitable awt
            ) COIO_STATIC_CALL_OP_CONST -> task<awaitable_await_result_t<Awaitable>> {
                co_return co_await starts_on_fn{}(sched, continues_on_fn{}(std::move(awt), sched));
            }

            template<typename Alloc, scheduler Scheduler, awaitable Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                std::allocator_arg_t, const Alloc& alloc, Scheduler&& sched, Awaitable awt
            )COIO_STATIC_CALL_OP_CONST -> task<awaitable_await_result_t<Awaitable>, Alloc> {
                co_return co_await starts_on_fn{}(
                    std::allocator_arg, alloc, sched, continues_on_fn{}(std::allocator_arg, alloc, std::move(awt), sched)
                );
            }
        };
    }

    inline constexpr detail::schedule_fn schedule{};
    inline constexpr detail::starts_on_fn starts_on{};
    inline constexpr detail::continues_on_fn continues_on{};
    inline constexpr detail::on_fn on{};

    struct inline_scheduler {
        using schedule_concept = detail::scheduler_tag;

        class schedule_sender {
#ifdef COIO_ENABLE_SENDERS
            struct env {
                template<typename T>
                static auto query(const detail::exec::get_completion_scheduler_t<T>&) noexcept -> inline_scheduler {
                    return {};
                }
            };
#endif
        public:
#ifdef COIO_ENABLE_SENDERS
            static auto get_env() noexcept -> env {
                return {};
            }
#endif
            auto operator co_await() const noexcept -> std::suspend_never {
                return {};
            }
        };

        [[nodiscard]]
        static auto schedule() noexcept -> schedule_sender {
            return {};
        }
    };

}