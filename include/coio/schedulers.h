#pragma once
#include <chrono>
#include <coroutine>
#include "task.h"
#include "detail/execution.h"

namespace coio {
    template<typename Scheduler>
    concept scheduler =
        std::derived_from<typename std::remove_cvref_t<Scheduler>::scheduler_concept, detail::scheduler_tag> and
        std::copyable<std::remove_cvref_t<Scheduler>> and
        std::equality_comparable<std::remove_cvref_t<Scheduler>> and
        requires (Scheduler&& sch)  {
            { static_cast<Scheduler&&>(sch).schedule() } -> awaitable_value;
        };

    template<typename Scheduler>
    concept timed_scheduler = scheduler<Scheduler> and requires (Scheduler&& sch) {
        { static_cast<Scheduler&&>(sch).now() } -> specialization_of<std::chrono::time_point>;
        { static_cast<Scheduler&&>(sch).schedule_after(static_cast<Scheduler&&>(sch).now().time_since_epoch()) } -> awaitable_value;
        { static_cast<Scheduler&&>(sch).schedule_at(static_cast<Scheduler&&>(sch).now()) } -> awaitable_value;
    };

    template<typename Scheduler>
    concept io_scheduler = scheduler<Scheduler> and
        std::derived_from<typename std::remove_cvref_t<Scheduler>::scheduler_concept, detail::io_scheduler_tag>;

    namespace detail {
        struct schedule_fn {
            template<scheduler Scheduler>
            COIO_STATIC_CALL_OP auto operator()(Scheduler&& sch) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(std::forward<Scheduler>(sch).schedule())) {
                return std::forward<Scheduler>(sch).schedule();
            }
        };

        struct starts_on_fn {
            template<scheduler Scheduler, awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                Scheduler sched, Awaitable awt
            ) COIO_STATIC_CALL_OP_CONST {
                return starts_on_fn{}(std::allocator_arg, std::allocator<void>{}, std::move(sched), std::move(awt));
            }

            template<typename Alloc, scheduler Scheduler, awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                std::allocator_arg_t, const Alloc&, Scheduler sched, Awaitable awt
            ) COIO_STATIC_CALL_OP_CONST -> task<await_result_t<Awaitable>, Alloc> {
                co_await std::forward<Scheduler>(sched).schedule();
                co_return co_await std::move(awt);
            }
        };

        struct continues_on_fn {
            template<scheduler Scheduler, awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                Awaitable awt, Scheduler sched
            ) COIO_STATIC_CALL_OP_CONST {
                return continues_on_fn{}(std::allocator_arg, std::allocator<void>{}, std::move(awt), std::move(sched));
            }

            template<typename Alloc, scheduler Scheduler, awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                std::allocator_arg_t, const Alloc&, Awaitable awt, Scheduler sched
            ) COIO_STATIC_CALL_OP_CONST -> task<await_result_t<Awaitable>, Alloc> {
                bool scheduled = false; // determine whether caught exception is thrown from `co_await sched.schdule()` or `co_await std::move(awt)`
                std::exception_ptr ex;
                try {
                    auto&& awaiter = get_awaiter(std::move(awt));
                    if constexpr (std::is_void_v<detail::await_result_t<Awaitable>>) {
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
            template<scheduler Scheduler, awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                Scheduler sched, Awaitable awt
            ) COIO_STATIC_CALL_OP_CONST {
                return on_fn{}(std::allocator_arg, std::allocator<void>{}, std::move(sched), std::move(awt));
            }

            template<typename Alloc, scheduler Scheduler, awaitable_value Awaitable>
            COIO_STATIC_CALL_OP auto operator()(
                std::allocator_arg_t, const Alloc& alloc, Scheduler sched, Awaitable awt
            )COIO_STATIC_CALL_OP_CONST -> task<await_result_t<Awaitable>, Alloc> {
                return starts_on_fn{}(
                    std::allocator_arg,
                    alloc,
                    sched,
                    continues_on_fn{}(
                        std::allocator_arg,
                        alloc,
                        std::move(awt),
                        sched
                    )
                );
            }
        };
    }

    inline constexpr detail::schedule_fn schedule{};
    inline constexpr detail::starts_on_fn starts_on{};
    inline constexpr detail::continues_on_fn continues_on{};
    inline constexpr detail::on_fn on{};

    struct inline_scheduler {
        using scheduler_concept = detail::scheduler_tag;

        class schedule_sender {
#ifdef COIO_ENABLE_SENDERS
            struct env {
                template<typename T>
                static auto query(const execution::get_completion_scheduler_t<T>&) noexcept -> inline_scheduler {
                    return {};
                }
            };
#endif
        public:
#ifdef COIO_ENABLE_SENDERS
            COIO_ALWAYS_INLINE static auto get_env() noexcept -> env {
                return {};
            }
#endif
            COIO_ALWAYS_INLINE auto operator co_await() const noexcept -> std::suspend_never {
                return {};
            }
        };

        [[nodiscard]]
        COIO_ALWAYS_INLINE static auto schedule() noexcept -> schedule_sender {
            return {};
        }

        friend auto operator== (const inline_scheduler&, const inline_scheduler&) noexcept -> bool {
            return true;
        }
    };

}