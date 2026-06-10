#pragma once
#include <memory>
#include <optional>
#include <coio/detail/concepts.h>
#include <coio/detail/execution.h>
#include <coio/utils/retain_ptr.h>
#include <coio/utils/scope_exit.h>
#include <coio/utils/stop_token.h>

namespace coio {
    class polymorphic_scheduler {
    private:
        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        struct state_base {
            using operation_state_concept = execution::operation_state_tag;
            using complete_fn_t = void(*)(state_base*) noexcept;

            state_base(complete_fn_t fn) noexcept : complete_(fn) {}

            state_base(const state_base&) = delete;

            auto operator= (const state_base&) -> state_base& = delete;

            const complete_fn_t complete_;
        };

        struct receiver {
            using receiver_concept = execution::receiver_tag;

            auto set_value() && noexcept -> void {
                COIO_ASSERT(state_ != nullptr);
                auto state = std::exchange(state_, nullptr);
                state->complete_(state);
            }

            state_base* state_;
        };

        struct state_holder {
            // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
            struct state_proxy_base {
                state_proxy_base() = default;

                state_proxy_base(const state_proxy_base&) = delete;

                auto operator= (const state_proxy_base&) -> state_proxy_base& = delete;

                virtual auto do_start() noexcept -> void = 0;

                virtual auto delete_self() noexcept -> void = 0;
            };

            // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
            template<execution::sender Sndr>
            struct state_proxy : state_proxy_base {
                using allocator_t = decltype(detail::get_suitable_allocator(std::declval<execution::env_of_t<Sndr>>()));

                state_proxy(Sndr sndr, receiver rcvr) :
                    alloc(detail::get_suitable_allocator(execution::get_env(sndr))),
                    inner(execution::connect(std::move(sndr), std::move(rcvr))) {}

                auto do_start() noexcept -> void override {
                    execution::start(inner);
                }

                auto delete_self() noexcept -> void override {
                    using alloc_t = std::allocator_traits<allocator_t>::template rebind_alloc<state_proxy>;
                    alloc_t al(std::move(alloc));
                    std::allocator_traits<alloc_t>::destroy(al, this);
                    std::allocator_traits<alloc_t>::deallocate(al, this, 1);
                }

                allocator_t alloc;
                execution::connect_result_t<Sndr, receiver> inner;
            };

            template<execution::sender Sndr>
            state_holder(Sndr sndr, receiver rcvr) {
                auto alloc = detail::get_suitable_allocator(execution::get_env(sndr));
                using alloc_t = std::allocator_traits<decltype(alloc)>::template rebind_alloc<state_proxy<Sndr>>;
                alloc_t al(std::move(alloc));
                auto ptr = std::allocator_traits<alloc_t>::allocate(al, 1);
                std::allocator_traits<alloc_t>::construct(al, ptr, std::move(sndr), std::move(rcvr));
                proxy = ptr;
            }

            state_holder(const state_holder&) = delete;

            ~state_holder() {
                proxy->delete_self();
            }

            auto operator= (const state_holder&) -> state_holder& = delete;

            // ReSharper disable once CppMemberFunctionMayBeConst
            auto do_start() noexcept -> void {
                COIO_ASSERT(proxy != nullptr);
                proxy->do_start();
            }

            state_proxy_base* proxy;
        };

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<execution::receiver Rcvr>
        struct state : state_base {
            state(Rcvr rcvr, auto* sndr_proxy) :
                state_base(&do_complete),
                rcvr_(std::move(rcvr)),
                holder_(sndr_proxy->do_connect(receiver{this})) {}

            auto start() & noexcept -> void {
                holder_.do_start();
            }

            static auto do_complete(state_base* self) noexcept -> void {
                auto this_ = static_cast<state*>(self);
                execution::set_value(std::move(this_->rcvr_));
            }

            Rcvr rcvr_;
            state_holder holder_;
        };

        class schedule_sender;

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        struct backend : retain_base<backend> {
            backend() = default;

            backend(const backend&) = delete;

            ~backend() = default;

            auto operator= (const backend&) -> backend& = delete;

            virtual auto schedule() -> schedule_sender = 0;

            virtual auto get_forward_progress_guarantee() const noexcept -> execution::forward_progress_guarantee = 0;

            virtual auto do_lose() noexcept -> void = 0;

            virtual auto equals(const backend*) const noexcept -> bool = 0;

            template<execution::scheduler Sched>
            auto try_get_scheduler() const noexcept -> std::optional<Sched> {
                if (auto self = dynamic_cast<const backend_sched<Sched>*>(this)) {
                    return self->sched;
                }
                return std::nullopt;
            }
        };

        class schedule_sender {
        private:
            // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
            struct sndr_proxy_base {
                sndr_proxy_base() = default;

                sndr_proxy_base(const sndr_proxy_base&) = delete;

                auto operator= (const sndr_proxy_base&) -> sndr_proxy_base& = delete;

                virtual auto do_connect(receiver) noexcept -> state_holder = 0;

                virtual auto delete_self() noexcept -> void = 0;
            };

            // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
            template<execution::scheduler Sched>
            struct sndr_proxy : sndr_proxy_base {
                using sched_sndr_t = execution::schedule_result_t<Sched>;

                sndr_proxy(Sched sched) : sndr_(execution::schedule(sched)) {}

                auto do_connect(receiver rcvr) noexcept -> state_holder override {
                    return state_holder(std::move(sndr_), std::move(rcvr));
                }

                auto delete_self() noexcept -> void override {
                    auto alloc = detail::get_suitable_allocator(execution::get_env(sndr_));
                    using alloc_t = std::allocator_traits<decltype(alloc)>::template rebind_alloc<sndr_proxy>;
                    alloc_t al(std::move(alloc));
                    std::allocator_traits<alloc_t>::destroy(al, this);
                    std::allocator_traits<alloc_t>::deallocate(al, this, 1);
                }

                sched_sndr_t sndr_;
            };

        public:
            using sender_concept = execution::sender_tag;
            using completion_signatures = execution::completion_signatures<execution::set_value_t()>;

            struct env {
                auto query(execution::get_completion_scheduler_t<execution::set_value_t>) const noexcept -> polymorphic_scheduler {
                    return polymorphic_scheduler(backend_);
                }

                retain_ptr<backend> backend_;
            };

        public:
            template<similar_to<schedule_sender>, typename...>
            static consteval auto get_completion_signatures() noexcept {
                return completion_signatures{};
            }

            template<execution::scheduler Sched>
            explicit schedule_sender(retain_ptr<backend> backend, Sched sched) : backend_(std::move(backend)) {
                auto sched_sndr = execution::schedule(sched);
                auto alloc = detail::get_suitable_allocator(execution::get_env(sched_sndr));
                using alloc_t = std::allocator_traits<decltype(alloc)>::template rebind_alloc<sndr_proxy<Sched>>;
                alloc_t al(std::move(alloc));
                auto ptr = std::allocator_traits<alloc_t>::allocate(al, 1);
                std::allocator_traits<alloc_t>::construct(al, ptr, std::move(sched));
                proxy_ = ptr;
            }

            schedule_sender(const schedule_sender&) = delete;

            schedule_sender(schedule_sender&& other) noexcept :
                backend_(std::move(other.backend_)),
                proxy_(std::exchange(other.proxy_, {})) {}

            ~schedule_sender() {
                if (proxy_) proxy_->delete_self();
            }

            auto operator= (schedule_sender other) noexcept -> schedule_sender& {
                std::ranges::swap(backend_, other.backend_);
                std::swap(proxy_, other.proxy_);
                return *this;
            }

            COIO_ALWAYS_INLINE auto swap(schedule_sender& other) noexcept -> void {
                std::swap(proxy_, other.proxy_);
            }

            COIO_ALWAYS_INLINE friend auto swap(schedule_sender& lhs, schedule_sender& rhs) noexcept -> void {
                lhs.swap(rhs);
            }

            template<execution::receiver Rcvr>
            COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                COIO_ASSERT(proxy_ != nullptr);
                scope_exit _{[this]() noexcept {
                    std::exchange(proxy_, nullptr)->delete_self();
                }};
                return state<Rcvr>{std::move(rcvr), proxy_};
            }

            COIO_ALWAYS_INLINE auto get_env() const noexcept -> env {
                COIO_ASSERT(proxy_ != nullptr);
                return env{backend_};
            }

        private:
            retain_ptr<backend> backend_;
            sndr_proxy_base* proxy_;
        };

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<typename Sched>
        struct backend_sched : backend {
            explicit backend_sched(Sched sched) noexcept : sched(std::move(sched)) {}

            auto schedule() -> schedule_sender override {
                return schedule_sender{retain_ptr<backend>{this}, sched};
            }

            auto get_forward_progress_guarantee() const noexcept -> execution::forward_progress_guarantee override {
                return execution::get_forward_progress_guarantee(sched);
            }

            auto equals(const backend* other_backend) const noexcept -> bool override {
                if (auto other = dynamic_cast<const backend_sched*>(other_backend)) {
                    return this->sched == other->sched;
                }
                return false;
            }

            COIO_NO_UNIQUE_ADDRESS Sched sched;
        };

        // ReSharper disable once CppPolymorphicClassWithNonVirtualPublicDestructor
        template<typename Sched, typename Alloc>
        struct backend_for : backend_sched<Sched> {
            using base = backend_sched<Sched>;

            explicit backend_for(Sched sched, Alloc alloc) noexcept : base(std::move(sched)), alloc(std::move(alloc)) {}

            auto do_lose() noexcept -> void override {
                using alloc_t = std::allocator_traits<Alloc>::template rebind_alloc<backend_for>;
                alloc_t al(std::move(alloc));
                std::allocator_traits<alloc_t>::destroy(al, this);
                std::allocator_traits<alloc_t>::deallocate(al, this, 1);
            }

            COIO_NO_UNIQUE_ADDRESS Alloc alloc;
        };

        template<typename Sched, typename Alloc>
        static auto create_backend(Sched sched, Alloc alloc) -> retain_ptr<backend> {
            using alloc_t = std::allocator_traits<Alloc>::template rebind_alloc<backend_for<Sched, Alloc>>;
            alloc_t al(alloc);
            auto ptr = std::allocator_traits<alloc_t>::allocate(al, 1);
            std::allocator_traits<alloc_t>::construct(al, ptr, std::move(sched), std::move(alloc));
            return retain_ptr<backend>(ptr);
        }

    public:
        using scheduler_concept = execution::scheduler_tag;

    private:
        explicit polymorphic_scheduler(retain_ptr<backend> backend) noexcept : backend_(std::move(backend)) {}

    public:
        template<different_from<polymorphic_scheduler> Sched, simple_allocator Alloc = std::allocator<void>>
            requires execution::scheduler<Sched> and infallible_scheduler<Sched, execution::env<>>
        explicit polymorphic_scheduler(Sched sched, Alloc alloc = Alloc()) : backend_(polymorphic_scheduler::create_backend(sched, alloc)) {}

        template<simple_allocator Alloc>
        explicit polymorphic_scheduler(polymorphic_scheduler sched, const Alloc&) noexcept : polymorphic_scheduler(std::move(sched)) {}

        [[nodiscard]]
        COIO_ALWAYS_INLINE auto schedule() const -> schedule_sender {
            COIO_ASSERT(backend_ != nullptr);
            return backend_->schedule();
        }

        COIO_ALWAYS_INLINE auto query(execution::get_forward_progress_guarantee_t) const noexcept -> execution::forward_progress_guarantee {
            COIO_ASSERT(backend_ != nullptr);
            return backend_->get_forward_progress_guarantee();
        }

        friend auto operator== (const polymorphic_scheduler& lhs, const polymorphic_scheduler& rhs) noexcept -> bool {
            if (lhs.backend_ == nullptr) {
                return rhs.backend_ == nullptr;
            }
            if (rhs.backend_ == nullptr) return false;
            return lhs.backend_->equals(rhs.backend_.get());
        }

        template<different_from<polymorphic_scheduler> Sched> requires execution::scheduler<Sched>
        auto operator== (const Sched& sched) const noexcept -> bool {
            if (std::optional<Sched> inner_sched = this->target<Sched>()) {
                return *inner_sched == sched;
            }
            return false;
        }

        COIO_ALWAYS_INLINE auto swap(polymorphic_scheduler& other) noexcept -> void {
            std::ranges::swap(backend_, other.backend_);
        }

        COIO_ALWAYS_INLINE friend auto swap(polymorphic_scheduler& lhs, polymorphic_scheduler& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

        template<different_from<polymorphic_scheduler> Sched> requires execution::scheduler<Sched> and unqualified_object<Sched>
        [[nodiscard]]
        COIO_ALWAYS_INLINE auto target() const noexcept -> std::optional<Sched> {
            return backend_ ? backend_->try_get_scheduler<Sched>() : std::nullopt;
        }

    private:
        retain_ptr<backend> backend_;
    };

    static_assert(execution::scheduler<polymorphic_scheduler>);
}

template<typename Alloc>
struct std::uses_allocator<coio::polymorphic_scheduler, Alloc> : std::true_type {};
