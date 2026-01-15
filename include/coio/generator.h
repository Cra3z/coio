#pragma once
#include <cassert>
#include <ranges>
#include <iterator>
#include <variant>
#include "detail/concepts.h"
#include "detail/co_memory.h"

namespace coio {
    template<std::ranges::range Range, typename Alloc = std::allocator<std::byte>>
    struct elements_of {
        COIO_NO_UNIQUE_ADDRESS Range range;
        COIO_NO_UNIQUE_ADDRESS Alloc allocator;
    };

    template<typename Range, typename Alloc = std::allocator<std::byte>>
    elements_of(Range&&, Alloc = Alloc()) -> elements_of<Range&&, Alloc>;

    template<typename Ref, typename Val = void, typename Alloc = void>
    class generator;

    namespace detail {

        template<typename Yielded>
        struct generator_promise_base_ {
            struct node_handle_t_ {

                node_handle_t_() = default;

                template<std::derived_from<generator_promise_base_> Promise>
                node_handle_t_(Promise& promise) noexcept : ptr_(std::addressof(promise)), coro_(std::coroutine_handle<Promise>::from_promise(promise)) {}

                auto get_yielded() ->Yielded {
                    COIO_ASSERT(ptr_ != nullptr);
                    return ptr_->get_yielded();
                }

                auto try_rethrow() ->void {
                    COIO_ASSERT(ptr_ != nullptr);
                    ptr_->try_rethrow();
                }

                auto get_yielded_or_throw() ->Yielded {
                    COIO_ASSERT(ptr_ != nullptr);
                    return ptr_->get_yielded_or_rethrow();
                }

                auto done() const noexcept ->bool {
                    COIO_ASSERT(coro_ != nullptr and ptr_ != nullptr);
                    return coro_.done();
                }

                auto prev() noexcept ->node_handle_t_& {
                    COIO_ASSERT(coro_ != nullptr and ptr_ != nullptr);
                    return ptr_->prev_;
                }

                auto prev() const noexcept ->const node_handle_t_& {
                    COIO_ASSERT(coro_ != nullptr and ptr_ != nullptr);
                    return ptr_->prev_;
                }

                auto top() noexcept ->node_handle_t_& {
                    COIO_ASSERT(coro_ != nullptr and ptr_ != nullptr);
                    return ptr_->top_;
                }

                auto top() const noexcept ->const node_handle_t_& {
                    COIO_ASSERT(coro_ != nullptr and ptr_ != nullptr);
                    return ptr_->top_;
                }

                auto coro() const noexcept ->std::coroutine_handle<> {
                    return coro_;
                }

                auto resume() ->void {
                    COIO_ASSERT(coro_ != nullptr and ptr_ != nullptr);
                    coro_.resume();
                }

                auto destroy() ->void {
                    COIO_ASSERT(coro_ != nullptr and ptr_ != nullptr);
                    coro_.destroy();
                }

                explicit operator bool() const noexcept {
                    // assume: `coro == nullptr` if and only if `ptr == nullptr`
                    COIO_ASSERT((coro_ == nullptr) == (ptr_ == nullptr));
                    return coro_ != nullptr;
                }

                auto operator-- () noexcept ->node_handle_t_& {
                    return (*this) = prev();
                }

                auto operator-- (int) noexcept ->node_handle_t_ {
                    auto tmp = *this;
                    --(*this);
                    return tmp;
                }

            private:
                generator_promise_base_* ptr_ = nullptr;
                std::coroutine_handle<> coro_;
            };

            struct const_lref_yield_awaiter_ {

                explicit const_lref_yield_awaiter_(const std::remove_reference_t<Yielded>& value) : value_(value) {}

                static auto await_ready() noexcept ->bool {
                    return false;
                }

                template<typename GeneratorPromise>
                auto await_suspend(std::coroutine_handle<GeneratorPromise> this_coro) noexcept ->void {
                    this_coro.promise().value_or_error_.template emplace<1>(std::addressof(value_));
                }

                static auto await_resume() noexcept ->void {}

                std::remove_cvref_t<Yielded> value_;
            };

            struct rvalue_generator_yield_awaiter_ {
                static auto await_ready() noexcept ->bool {
                    return false;
                }

                template<typename OtherGenerator>
                auto await_suspend(std::coroutine_handle<OtherGenerator> this_coro) ->std::coroutine_handle<> {
                    COIO_ASSERT(new_top_);
                    node_handle_t_ h = this_coro.promise();
                    new_top_.top() = new_top_;
                    new_top_.prev() = h;
                    for (; h; --h) h.top() = new_top_;
                    return new_top_.coro();
                }

                auto await_resume() ->void {
                    new_top_.try_rethrow();
                }

                node_handle_t_ new_top_;
            };

            struct range_yield_awaiter_ : rvalue_generator_yield_awaiter_ {
                ~range_yield_awaiter_() {
                    if (this->new_top_) this->new_top_.destroy();
                }
            };

            struct final_awaiter_ {
                static auto await_ready() noexcept -> bool {
                    return false;
                }

                auto await_suspend(std::coroutine_handle<>) const noexcept -> std::coroutine_handle<> {
                    if (prev_) {
                        for (auto h = prev_; h; --h) h.top() = prev_;
                        return prev_.coro();
                    }
                    return std::noop_coroutine();
                }

                static auto await_resume() noexcept ->void {}

                node_handle_t_ prev_;
            };

            auto await_transform() ->void = delete;

            static auto initial_suspend() noexcept -> std::suspend_always {
                return {};
            }

            auto final_suspend() noexcept ->final_awaiter_ {
                return {prev_};
            }

            auto return_void() const noexcept ->void {}

            auto unhandled_exception() -> void {
                if (not top_.prev()) throw;
                value_or_error_.template emplace<2>(std::current_exception());
            }

            auto yield_value(Yielded value) noexcept ->std::suspend_always {
                value_or_error_.template emplace<1>(std::addressof(value));
                return {};
            }

            auto yield_value(const std::remove_reference_t<Yielded>& value) requires std::is_rvalue_reference_v<Yielded> and
                std::constructible_from<std::remove_cvref_t<Yielded>, const std::remove_reference_t<Yielded>&>
            {
                return const_lref_yield_awaiter_{value};
            }

            auto get_yielded() ->Yielded {
                COIO_ASSERT(value_or_error_.index() == 1);
                return static_cast<Yielded>(**std::get_if<1>(&value_or_error_));
            }

            auto get_yielded_or_rethrow() ->Yielded {
                COIO_ASSERT(value_or_error_.index() > 0);
                try_rethrow();
                return static_cast<Yielded>(**std::get_if<1>(&value_or_error_));
            }

            auto try_rethrow() ->void {
                if (value_or_error_.index() == 2) std::rethrow_exception(*std::get_if<2>(&value_or_error_));
            }

            node_handle_t_ prev_, top_;
            std::variant<std::monostate, std::add_pointer_t<Yielded>, std::exception_ptr> value_or_error_;
        };


        template<typename Generator>
        struct generator_promise_ : generator_promise_base_<typename Generator::yielded>, promise_alloc_control<typename Generator::allocator_type> {

            using yielded = typename Generator::yielded;
            using allocator_type = typename Generator::allocator_type;
            using base = generator_promise_base_<yielded>;

            using typename base::node_handle_t_;
            using typename base::rvalue_generator_yield_awaiter_;
            using typename base::range_yield_awaiter_;

            using base::yield_value;

            template<typename Ref2, typename Val2, typename Alloc2, typename IgnoredAlloc> requires std::same_as<typename generator<Ref2, Val2, Alloc2>::yielded, yielded>
            auto yield_value(elements_of<generator<Ref2, Val2, Alloc2>&&, IgnoredAlloc> elems) noexcept ->rvalue_generator_yield_awaiter_ {
                return {node_handle_t_{elems.range.coro_.promise()}};
            }

            template<std::ranges::input_range InputRange, typename Alloc> requires std::convertible_to<std::ranges::range_reference_t<InputRange>, yielded>
            auto yield_value(elements_of<InputRange, Alloc> elems) ->range_yield_awaiter_ {
                static constexpr auto to_generator_ = [](std::allocator_arg_t, Alloc, std::ranges::iterator_t<InputRange> first, std::ranges::sentinel_t<InputRange> last) -> generator<yielded, std::ranges::range_value_t<InputRange>, Alloc> {
                    for (; first != last; ++first) co_yield static_cast<yielded>(*first);
                };
                auto gen_ = to_generator_(std::allocator_arg, elems.allocator, std::ranges::begin(elems.range), std::ranges::end(elems.range));
                return {std::exchange(gen_.coro_, {}).promise()};
            }

            auto get_return_object() noexcept ->Generator {
                return {std::coroutine_handle<generator_promise_>::from_promise(*this)};
            }

        };

        template<typename Yielded>
        using gen_stack_node_handle_t = typename generator_promise_base_<Yielded>::node_handle_t_;

    }


    template<typename Ref, typename Val, typename Alloc>
    class [[nodiscard]] generator : public std::ranges::view_interface<generator<Ref, Val, Alloc>> {

        template<typename Generator>
        friend struct detail::generator_promise_;

        using value_type = std::conditional_t<std::is_void_v<Val>, std::remove_cvref_t<Ref>, Val>;
        using reference  = std::conditional_t<std::is_void_v<Val>, Ref&&, Ref>;
        using rref = std::conditional_t<std::is_reference_v<reference>, std::remove_reference_t<reference>&&, reference>;
        using allocator_type = Alloc;

        static_assert(std::is_void_v<allocator_type> or simple_allocator<allocator_type>);
        static_assert(unqualified_object<value_type>);
        static_assert(
            std::is_reference_v<reference> or
            (unqualified_object<reference> and std::copy_constructible<reference>)
        );
        static_assert(
            std::common_reference_with<reference&&, value_type&> and
            std::common_reference_with<reference&&, rref&&> and
            std::common_reference_with<rref&&, const value_type&>
        );

    public:

        using yielded = std::conditional_t<std::is_reference_v<reference>, reference, const reference&>;

        using promise_type = detail::generator_promise_<generator>;

        class iterator {
        public:

            using value_type = generator::value_type;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::input_iterator_tag;

        public:

            explicit iterator(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro) {}

            iterator(iterator&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

            auto operator= (iterator&& other) noexcept ->iterator& {
                coro_ = std::exchange(other.coro_, {});
                return *this;
            }

            [[nodiscard]]
            auto operator* () const noexcept(std::is_nothrow_copy_constructible_v<reference>) ->reference {
                detail::gen_stack_node_handle_t<yielded> h{coro_.promise()};
                return static_cast<reference>(h.top().get_yielded());
            }

            auto operator++ () -> iterator& {
                detail::gen_stack_node_handle_t<yielded> h{coro_.promise()};
                h.top().resume();
                return *this;
            }

            auto operator++ (int) ->void {
                ++*this;
            }

            friend auto operator== (const iterator& it, std::default_sentinel_t) noexcept ->bool {
                return it.coro_.done();
            }

        private:
            std::coroutine_handle<promise_type> coro_;
        };

    private:

        generator(std::coroutine_handle<promise_type> handle) noexcept : coro_(handle) {}

    public:

        generator(const generator&) = delete;

        generator(generator&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

        ~generator() {
            if (coro_) coro_.destroy();
        }

        auto operator= (generator other) noexcept ->generator& {
            std::swap(coro_, other.coro_);
            return *this;
        }

        [[nodiscard]]
        auto begin() ->iterator {
            COIO_ASSERT(coro_ != nullptr);
            auto& promise = coro_.promise();
            promise.top_ = promise;
            coro_.resume();
            return iterator{coro_};
        }

        [[nodiscard]]
        auto end() const noexcept ->std::default_sentinel_t {
            return std::default_sentinel;
        }

    private:

        std::coroutine_handle<promise_type> coro_;

    };

}