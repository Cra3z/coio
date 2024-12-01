#pragma once
#include <atomic>
#include <coroutine>
#include <chrono>
#include <deque>
#include <mutex>
#include <queue>
#include <ranges>
#include <stop_token>
#include <tuple>
#include <variant>
#include <vector>
#include <functional>
#include "config.h"
#include "concepts.h"
#include "detail/co_memory.h"
#include "detail/task_error.h"
#include "utils/retain_ptr.h"
#include "utils/type_traits.h"

namespace coio {

	template<awaiter T>
	using await_result_t = decltype(std::declval<T>().await_resume());

	template<awaitable T>
	struct awaitable_traits {
		using awaiter_type = typename detail::get_awaiter<T>::type;

		using await_result_type = await_result_t<awaiter_type>;
	};

	struct nothing {};

	template<typename T = void, typename Alloc = void>
	class [[nodiscard]] task;

	template<typename T = void, typename Alloc = void>
	class [[nodiscard]] shared_task;

	class io_context {
	private:
		struct timed_coro {

			std::chrono::steady_clock::time_point timeout_time;

			std::coroutine_handle<> coro;

			friend auto operator== (const timed_coro& lhs, const timed_coro& rhs) noexcept ->bool {
				return lhs.timeout_time == rhs.timeout_time;
			}

			friend auto operator<=> (const timed_coro& lhs, const timed_coro& rhs) noexcept {
				return lhs.timeout_time <=> rhs.timeout_time;
			}

		};
	public:

	    class work_guard {
	    public:
	        explicit work_guard(io_context& ctx) noexcept : ctx_(&ctx) {
	            ctx_->begin_work();
	        }

            work_guard(const work_guard& other) noexcept : work_guard(*other.ctx_) {}

	        ~work_guard() {
	            ctx_->end_work();
	        }

            auto operator= (work_guard other) noexcept ->work_guard& {
	            std::swap(ctx_, other.ctx_);
	            return *this;
	        }
	    private:
	        io_context* ctx_;
	    };

	public:

		[[nodiscard]]
		static auto instance() ->io_context& {
			static io_context instance_{};
			return instance_;
		}

		io_context() = default;

		io_context(const io_context&) = delete;

		auto operator= (const io_context&) -> io_context& = delete;

		auto post_timer(std::chrono::steady_clock::time_point timeout_time, std::coroutine_handle<> coro) ->void {
			std::scoped_lock lock{mtx_};
			sleeping_coros.emplace(timeout_time, coro);
		}

		auto post(std::coroutine_handle<> coro) ->void {
			std::scoped_lock lock{mtx_};
			ready_coros.push_back(coro);
		}

		auto post_wait(std::coroutine_handle<> coro) ->void {}

		auto run() ->void {
			while (not stop_.stop_requested()) {
				[[maybe_unused]] std::scoped_lock lock{mtx_};
				auto now = std::chrono::steady_clock::now();
				while (not sleeping_coros.empty()) {
					auto [timeout_time, coro] = sleeping_coros.top();
					if (timeout_time <= now) {
						ready_coros.push_back(coro);
						sleeping_coros.pop();
					}
					else break;
				}

				while (not ready_coros.empty()) {
					auto coro = ready_coros.front();
					coro.resume();
					ready_coros.pop_front();
				}

				if (sleeping_coros.empty() and work_count_ == 0) break;
			}
		}

		auto stop() noexcept ->void {
			stop_.request_stop();
		}

		[[nodiscard]]
		auto stop_requested() const noexcept -> bool {
			return stop_.stop_requested();
		}

	    auto begin_work() noexcept ->void {
            ++work_count_;
		}

	    auto end_work() noexcept ->void {
            --work_count_;
		}

	private:
		std::recursive_mutex mtx_;
		std::stop_source stop_;
		std::deque<std::coroutine_handle<>> ready_coros;
		std::priority_queue<timed_coro, std::vector<timed_coro>, std::greater<>> sleeping_coros;
	    std::atomic<std::size_t> work_count_{0};
	};



	namespace detail {
		template<typename T>
		using awaitable_await_result_t = typename awaitable_traits<T>::await_result_type;

	    template<typename T>
        using void_to_nothing = std::conditional_t<std::is_void_v<T>, nothing, T>;

	    template<typename T>
	    using awaitable_non_void_await_result_t = void_to_nothing<awaitable_await_result_t<T>>;

	    template<typename>
		struct task_traits;

		template<typename T, typename Alloc>
		struct task_traits<task<T, Alloc>> {
			using elem_t = T;
			using alloc_t = Alloc;
		    using promise_t = typename std::coroutine_traits<task<T, Alloc>>::promise_type;
		};

		template<typename T, typename Alloc>
		struct task_traits<shared_task<T, Alloc>> {
			using elem_t = T;
			using alloc_t = Alloc;
		    using promise_t = typename std::coroutine_traits<shared_task<T, Alloc>>::promise_type;
		};

		template<typename T>
		using task_elem_t = typename task_traits<T>::elem_t;

		template<typename T>
		using task_alloc_t = typename task_traits<T>::alloc_t;

	    template<typename T>
	    using task_promise_t = typename task_traits<T>::promise_t;

		template<typename T>
    	class promise_return_control {
		public:

			auto return_value(T value) noexcept(std::is_nothrow_constructible_v<wrap_ref_t<T>, T>) ->void {
				result_.template emplace<1>(static_cast<T&&>(value));
			}

		    auto unhandled_exception() noexcept ->void {
			    result_.template emplace<2>(std::current_exception());
			}

		    auto get_result() ->T& {
			    COIO_ASSERT(result_.index() > 0);
			    if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
			    return *std::get_if<1>(&result_);
			}

		private:
			std::variant<std::monostate, wrap_ref_t<T>, std::exception_ptr> result_;
		};

    	template<>
		class promise_return_control<void> {
    	public:

    	    auto return_void() noexcept ->void {
				result_.emplace<1>();
    		}

    		auto unhandled_exception() noexcept ->void {
    			result_.emplace<2>(std::current_exception());
    		}

    	    auto get_result() ->void {
    		    COIO_ASSERT(result_.index() > 0);
    		    if (result_.index() == 2) std::rethrow_exception(*std::get_if<2>(&result_));
    		}

    	private:
    		std::variant<std::monostate, nothing, std::exception_ptr> result_;
    	};

	    struct sleep_awaiter {
	        static auto await_ready() noexcept ->bool {
	            return false;
	        }

	        auto await_suspend(std::coroutine_handle<> this_coro) const noexcept ->void ;

	        static auto await_resume() noexcept ->void {}

	        std::chrono::steady_clock::time_point timeout_time;
	    };

		template<typename TaskType>
		struct task_awaiter {
			static auto await_ready() noexcept -> bool {
				return false;
			}

			auto await_suspend(std::coroutine_handle<> this_coro) const noexcept -> std::coroutine_handle<> {
				COIO_ASSERT(coro != nullptr);
				coro.promise().prev_coro_ = this_coro;
				return coro;
			}

			auto await_resume() const ->task_elem_t<TaskType> {
				auto& promise = coro.promise();
				return static_cast<std::add_rvalue_reference_t<task_elem_t<TaskType>>>(promise.get_result());
			}

			std::coroutine_handle<task_promise_t<TaskType>> coro;
		};

	    struct task_final_awaiter {
	        static auto await_ready() noexcept -> bool {
	            return false;
	        }

	        auto await_suspend(std::coroutine_handle<>) const noexcept -> std::coroutine_handle<> {
	            if (prev_coro) return prev_coro;
	            return std::noop_coroutine();
	        }

	        static auto await_resume() noexcept ->void {}

	        std::coroutine_handle<> prev_coro;
	    };

        struct shared_task_node {
            std::coroutine_handle<> waiter;
            shared_task_node* next = nullptr;
        };

	    template<typename SharedTaskType>
        struct shared_task_awaiter : shared_task_node {

	        shared_task_awaiter(std::coroutine_handle<task_promise_t<SharedTaskType>> coro) noexcept : coro(coro) {}

	        auto await_ready() noexcept -> bool {
	            COIO_ASSERT(coro != nullptr);
	            return coro.promise().step_ == 2;
	        }

	        auto await_suspend(std::coroutine_handle<> this_coro) noexcept ->std::coroutine_handle<> {
                waiter = this_coro;
	            next = coro.promise().head_.exchange(this, std::memory_order_acq_rel);
	            if (int zero = 0; coro.promise().step_.compare_exchange_strong(zero, 1)) {
	                return coro;
	            }
	            return std::noop_coroutine();
	        }

	        auto await_resume() ->add_const_lvalue_ref_t<task_elem_t<SharedTaskType>> {
	            return coro.promise().get_result();
	        }

	        std::coroutine_handle<task_promise_t<SharedTaskType>> coro;
	    };

        struct shared_task_final_awaiter {
            static auto await_ready() noexcept ->bool {
                return false;
            }

            template<typename SharedTaskPromise>
            static auto await_suspend(std::coroutine_handle<SharedTaskPromise> this_coro) noexcept ->void {
                shared_task_node* awaiter_ = this_coro.promise().head_.load(std::memory_order_acquire);
                while (awaiter_) {
                    auto next = awaiter_->next;
                    awaiter_->waiter.resume();
                    awaiter_ = next;
                }
            }

            static auto await_resume() noexcept ->void {}
        };

	    template<typename TaskType>
        struct task_promise : promise_return_control<task_elem_t<TaskType>>, promise_alloc_control<task_alloc_t<TaskType>> {
	        friend task_awaiter<TaskType>;

	        auto get_return_object() noexcept ->TaskType {
	            return std::coroutine_handle<task_promise>::from_promise(*this);
	        }

	        static auto initial_suspend() noexcept ->std::suspend_always {
	            return {};
	        }

	        auto final_suspend() const noexcept ->task_final_awaiter {
	            return {prev_coro_};
	        }

	        template<typename Rep, typename Period>
            auto await_transform(std::chrono::duration<Rep, Period> duration) noexcept ->sleep_awaiter {
	            return {.timeout_time = std::chrono::steady_clock::now() + duration};
	        }

	        decltype(auto) await_transform(awaitable_for<task_promise> auto&& awt) noexcept {
	            return std::forward<decltype(awt)>(awt);
	        }

	    private:
	        std::coroutine_handle<> prev_coro_;
	    };

        template<typename SharedTaskType>
        struct shared_task_promise : promise_return_control<task_elem_t<SharedTaskType>>, promise_alloc_control<task_alloc_t<SharedTaskType>> {
            friend shared_task_final_awaiter;
            friend shared_task_awaiter<SharedTaskType>;

            using handle_type = std::coroutine_handle<shared_task_promise>;

            auto get_return_object() noexcept ->SharedTaskType {
                return std::coroutine_handle<shared_task_promise>::from_promise(*this);
            }

            static auto initial_suspend() noexcept ->std::suspend_always {
                return {};
            }

            auto final_suspend() noexcept ->shared_task_final_awaiter {
                step_ = 2;
                step_.notify_all();
                return {};
            }

            template<typename Rep, typename Period>
            auto await_transform(std::chrono::duration<Rep, Period> duration) noexcept ->sleep_awaiter {
                return {.timeout_time = std::chrono::steady_clock::now() + duration};
            }

            decltype(auto) await_transform(awaitable_for<shared_task_promise> auto&& awt) noexcept {
                return std::forward<decltype(awt)>(awt);
            }

            auto retain() noexcept ->void {
                ref_count_.fetch_add(1, std::memory_order_relaxed);
            }

            auto lose() noexcept ->void {
                if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    auto handle = handle_type::from_promise(*this);
                    handle.destroy();
                }
            }

        private:
            std::atomic<shared_task_node*> head_{nullptr};
            std::atomic<std::size_t> ref_count_{0};
            std::atomic<int> step_{0}; // 0: not started, 1: started, 2: finished
        };

    }

	template<typename T, typename Alloc>
	class task {
		static_assert(
		    std::same_as<T, void> or
		    (unqualified_object<T> and std::move_constructible<T>) or
		    std::is_lvalue_reference_v<T>,
		    "type `T` shall be `void` or a move-constructible object-type or a lvaue-reference type."
		);

	    static_assert(
            detail::valid_coroutine_alloctor_<Alloc>,
            "type `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
        );

		friend detail::task_promise<task>;

	public:

		using promise_type = detail::task_promise<task>;

	private:

		task(std::coroutine_handle<promise_type> coro) noexcept : coro_{coro} {}

	public:

		task() = default;

		task(const task&) = delete;

		task(task&& other) noexcept : coro_{std::exchange(other.coro_, {})} {}

		~task() {
			if (coro_) coro_.destroy();
		}

		auto operator= (const task&) -> task& = delete;

		auto operator= (task&& other) noexcept -> task& {
			task(std::move(other)).swap(*this);
			return *this;
		}

	    explicit operator bool() const noexcept {
		    return coro_ != nullptr;
		}

		auto operator co_await() const -> detail::task_awaiter<task> {
			if (coro_ == nullptr) [[unlikely]] throw task_error{task_errc::no_state};
			if (coro_.done()) [[unlikely]] throw task_error{task_errc::already_retrieved};
			return {coro_};
		}

		auto swap(task& other) noexcept ->void {
			std::swap(coro_, other.coro_);
		}

		friend auto swap(task& lhs, task& rhs) noexcept -> void {
			lhs.swap(rhs);
		}

		[[nodiscard]]
		auto ready() const noexcept ->bool {
		    COIO_ASSERT(coro_ != nullptr);
		    return coro_.done();
		}

		auto reset() noexcept ->void {
			task{}.swap(*this);
		}

	private:
		std::coroutine_handle<promise_type> coro_;
	};

	inline auto detail::sleep_awaiter::await_suspend(std::coroutine_handle<> this_coro) const noexcept -> void {
		io_context::instance().post_timer(timeout_time, this_coro);
	}

    template<typename T, typename Alloc>
    class shared_task {
	    static_assert(
            std::same_as<T, void> or
            unqualified_object<T> or
            std::is_lvalue_reference_v<T>,
            "type `T` shall be `void` or an object-type or a lvaue-reference type."
        );
	    static_assert(
	        detail::valid_coroutine_alloctor_<Alloc>,
	        "typename `Alloc` shall be `void` or an allocator-type whose `typename std::allocator_traits<Alloc>::pointer` is a pointer-type."
	    );

        friend detail::shared_task_promise<shared_task>;

	public:

        using promise_type = detail::shared_task_promise<shared_task>;

    private:

        shared_task(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro), retain_(coro ? &coro.promise() : nullptr) {} // NOLINT

    public:

        shared_task() noexcept = default;

        shared_task(const shared_task& other) = default;

        shared_task(shared_task&& other) noexcept : coro_(std::exchange(other.coro_, {})), retain_(std::move(other.retain_)) {}

        ~shared_task() = default;

        auto operator= (shared_task other) noexcept ->shared_task& {
            shared_task{std::move(other)}.swap(*this);
            return *this;
        }

        explicit operator bool() const noexcept {
            return coro_ != nullptr;
        }

        auto operator co_await() const ->detail::shared_task_awaiter<shared_task> {
            if (coro_ == nullptr) [[unlikely]] throw task_error{task_errc::no_state};
            return {coro_};
        }

	    [[nodiscard]]
        auto ready() const noexcept ->bool {
            COIO_ASSERT(coro_ != nullptr);
            return coro_.promise().step_ == 2;
        }

        auto swap(shared_task& other) noexcept ->void {
            std::ranges::swap(coro_, other.coro_);
            std::ranges::swap(retain_, other.retain_);
        }

        friend auto swap(shared_task& lhs, shared_task& rhs) noexcept -> void {
            lhs.swap(rhs);
        }

	    auto reset() noexcept ->void {
            shared_task{}.swap(*this);
        }

    private:
        std::coroutine_handle<promise_type> coro_;
        retain_ptr<promise_type> retain_;
    };

	template<typename Awaiter, typename Fn>
	struct transform_awaiter {

		static_assert(awaiter<Awaiter>);
		static_assert(std::invocable<Fn, await_result_t<Awaiter>>);

		auto await_ready() noexcept(noexcept(std::declval<Awaiter&>().await_ready())) {
			return inner_awaiter.await_ready();
		}

		auto await_suspend(std::coroutine_handle<> this_coro) noexcept(noexcept(std::declval<Awaiter&>().await_suspend(std::declval<std::coroutine_handle<>>()))) {
			return inner_awaiter.await_suspend(this_coro);
		}

		decltype(auto) await_resume() noexcept(noexcept(std::invoke(std::declval<Fn>(), std::declval<Awaiter&>().await_resume()))) {
			return std::invoke(transform, inner_awaiter.await_resume());
		}

		Awaiter inner_awaiter;
		Fn transform;

	};

	namespace detail {

		struct make_transform_awaiter_fn {
			template<awaiter Awaiter, std::invocable<await_result_t<Awaiter>> Fn> requires std::move_constructible<Awaiter> and std::move_constructible<Fn>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (Awaiter awaiter, Fn fn) COIO_STATIC_CALL_OP_CONST noexcept(std::is_nothrow_move_constructible_v<Awaiter> and std::is_nothrow_move_constructible_v<Fn>) ->transform_awaiter<Awaiter, Fn> {
				return transform_awaiter{std::move(awaiter), std::move(fn)};
			}
		};

		struct when_all_counter {
			explicit when_all_counter(std::size_t count) noexcept : count(count) {
				COIO_ASSERT(count > 0);
			}

			[[nodiscard]]
			auto decrease() noexcept ->std::coroutine_handle<> {
				if (--count == 0) return prev_coro;
				return std::noop_coroutine();
			}

			std::atomic<std::size_t> count;
			std::coroutine_handle<> prev_coro;
		};

		template<typename T, typename OnFinalSuspend>
		struct task_wrapper {
            static_assert(std::default_initializable<OnFinalSuspend> and std::is_nothrow_invocable_r_v<std::coroutine_handle<>, OnFinalSuspend>);

		    struct promise_type : promise_return_control<T> {
				auto get_return_object() noexcept -> task_wrapper {
					return {std::coroutine_handle<promise_type>::from_promise(*this)};
				}

				static auto initial_suspend() noexcept -> std::suspend_always { return {}; }

				auto final_suspend() noexcept -> task_final_awaiter {
					return {on_final_suspend_()};
				}

		        OnFinalSuspend on_final_suspend_;
			};

			task_wrapper(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro) {}

			task_wrapper(const task_wrapper&) = delete;

			task_wrapper(task_wrapper&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

			~task_wrapper() {
				if (coro_) coro_.destroy();
			}

			auto operator= (const task_wrapper&) = delete;

			auto operator= (task_wrapper&& other) noexcept -> task_wrapper& {
				task_wrapper(std::move(other)).swap(*this);
				return *this;
			}

			auto swap(task_wrapper& other) noexcept ->void {
				std::swap(coro_, other.coro_);
			}

			friend auto swap(task_wrapper& lhs, task_wrapper& rhs) noexcept ->void {
				lhs.swap(rhs);
			}

			decltype(auto) get_result() const {
				return coro_.promise().get_result();
			}

		    decltype(auto) get_non_void_result() const {
			    if constexpr (std::is_void_v<T>) {
                    get_result();
			        return nothing{};
			    }
			    else return get_result();;
			}

			std::coroutine_handle<promise_type> coro_;
		};

        struct on_when_all_task_final_suspend_fn {
            COIO_STATIC_CALL_OP auto operator() () COIO_STATIC_CALL_OP_CONST noexcept ->std::coroutine_handle<> {
                COIO_ASSERT(counter_ != nullptr);
                return counter_->decrease();
            }
            when_all_counter* counter_ = nullptr;
        };

	    template<typename T>
	    using when_all_task = task_wrapper<T, on_when_all_task_final_suspend_fn>;

		template<typename>
		class when_all_ready_awaiter;

		template<elements_move_insertable_range WhenAllTasks> requires std::move_constructible<WhenAllTasks> and specialization_of<std::ranges::range_value_t<WhenAllTasks>, task_wrapper>
		class when_all_ready_awaiter<WhenAllTasks> {
		public:
			when_all_ready_awaiter(WhenAllTasks when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)), counter_(std::make_unique<when_all_counter>(std::ranges::distance(when_all_tasks_))) {}

			auto await_ready() const noexcept -> bool {
				return bool(counter_->prev_coro);
			}

			auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
				counter_->prev_coro = this_coro;
				for (auto& task : when_all_tasks_) {
					task.coro_.promise().on_final_suspend_.counter_ = counter_.get();
					task.coro_.resume();
				};
			}

			[[nodiscard]]
			auto await_resume() noexcept(std::is_nothrow_move_constructible_v<WhenAllTasks>) -> WhenAllTasks {
				return std::move(when_all_tasks_);
			}

		private:
			WhenAllTasks when_all_tasks_;
			std::unique_ptr<when_all_counter> counter_;
		};

		template<typename... Types>
		class when_all_ready_awaiter<std::tuple<when_all_task<Types>...>> {
		public:

			when_all_ready_awaiter(std::tuple<when_all_task<Types>...> when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)) {}

			auto await_ready() const noexcept -> bool {
				return bool(counter_->prev_coro);
			}

			auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
				counter_->prev_coro = this_coro;
				[this]<std::size_t... I>(std::index_sequence<I...>) {
					(..., (std::get<I>(when_all_tasks_).coro_.promise().on_final_suspend_.counter_ = counter_.get()));
					(..., std::get<I>(when_all_tasks_).coro_.resume());
				}(std::make_index_sequence<sizeof...(Types)>{});
			}

			[[nodiscard]]
			auto await_resume() noexcept(std::is_nothrow_move_constructible_v<std::tuple<when_all_task<Types>...>>) -> std::tuple<when_all_task<Types>...> {
				return std::move(when_all_tasks_);
			}

		private:
			std::tuple<when_all_task<Types>...> when_all_tasks_;
			std::unique_ptr<when_all_counter> counter_ = std::make_unique<when_all_counter>(sizeof...(Types));
		};

		template<typename... Types>
		when_all_ready_awaiter(std::tuple<when_all_task<Types>...>) -> when_all_ready_awaiter<std::tuple<when_all_task<Types>...>>;

		inline constexpr auto do_when_all_task = []<typename Awaitable>(Awaitable awaitable) COIO_STATIC_CALL_OP ->when_all_task<awaitable_await_result_t<Awaitable>> {
			if constexpr (std::is_void_v<awaitable_await_result_t<Awaitable>>) {
				co_await awaitable;
				co_return;
			}
			else co_return co_await awaitable;
		};

		template<typename ResultStorageRange>
		struct store_results_in_t {};

		template<unqualified_object TaskStorageRange>
		inline constexpr store_results_in_t<TaskStorageRange> store_results_in{};

		struct when_all_ready_fn {
			template<awaitable... Awaitables>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
				return when_all_ready_awaiter{
					std::tuple{
						do_when_all_task(std::forward<Awaitables>(awaitables))...
					}
				};
			}

			template<std::forward_iterator It, std::sentinel_for<It> St> requires awaitable<std::iter_value_t<It>> and std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (It first, St last) COIO_STATIC_CALL_OP_CONST {
				using when_all_task_container_t = std::vector<
					when_all_task<
						awaitable_await_result_t<std::iter_value_t<It>>
					>
				>;
				when_all_task_container_t result;
				while (first != last) {
					void(result.push_back(do_when_all_task(*(first++))));
				}
				return when_all_ready_awaiter<when_all_task_container_t>{std::move(result)};
			}

			template<borrowed_forward_range TaskRange> requires awaitable<std::ranges::range_value_t<TaskRange>> and std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng) COIO_STATIC_CALL_OP_CONST {
				return (*this)(std::ranges::begin(rng), std::ranges::end(rng));
			}

		};

		struct when_all_fn {
			template<awaitable... Awaitables>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
				return transform_awaiter{
					when_all_ready_fn{}(std::forward<Awaitables>(awaitables)...),
					[]<typename... Types>(std::tuple<when_all_task<Types>...> when_all_tasks_) {
						return [&when_all_tasks_]<std::size_t... I>(std::index_sequence<I...>) {
							return std::tuple<void_to_nothing<Types>...>{std::get<I>(when_all_tasks_).get_non_void_result()...};
						}(std::make_index_sequence<sizeof...(Types)>{});
					}
				};
			}

			template<std::forward_iterator It, std::sentinel_for<It> St, elements_move_insertable_range ResultStorageRange>
				requires awaitable<std::iter_value_t<It>> and
						std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>> and
						std::default_initializable<ResultStorageRange> and
						std::move_constructible<ResultStorageRange> and
						std::convertible_to<awaitable_non_void_await_result_t<std::iter_value_t<It>>, std::ranges::range_value_t<ResultStorageRange>>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (It first, St last, store_results_in_t<ResultStorageRange>) COIO_STATIC_CALL_OP_CONST {
				return transform_awaiter{
					when_all_ready_fn{}(first, last),
					[]<typename T>(std::vector<when_all_task<T>> when_all_tasks_) {
						ResultStorageRange result_storage_range;
						for (auto& task : when_all_tasks_) {
						    void(result_storage_range.insert(std::ranges::end(result_storage_range), task.get_non_void_result()));
						}
						return result_storage_range;
					}
				};
			}

			template<std::forward_iterator It, std::sentinel_for<It> St> requires awaitable<std::iter_value_t<It>> and std::constructible_from<std::iter_value_t<It>, std::iter_reference_t<It>>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (It first, St last) COIO_STATIC_CALL_OP_CONST {
				return (*this)(first, last, store_results_in<std::vector<awaitable_non_void_await_result_t<std::iter_value_t<It>>>>);
			}

			template<borrowed_forward_range TaskRange, elements_move_insertable_range ResultStorageRange>
				requires awaitable<std::ranges::range_value_t<TaskRange>> and
						std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>> and
						std::default_initializable<ResultStorageRange> and
						std::move_constructible<ResultStorageRange> and
						std::convertible_to<awaitable_non_void_await_result_t<std::ranges::range_value_t<TaskRange>>, std::ranges::range_value_t<ResultStorageRange>>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng, store_results_in_t<ResultStorageRange>) COIO_STATIC_CALL_OP_CONST {
				return (*this)(std::ranges::begin(rng), std::ranges::end(rng), store_results_in<ResultStorageRange>);
			}

			template<borrowed_forward_range TaskRange> requires awaitable<std::ranges::range_value_t<TaskRange>> and std::constructible_from<std::ranges::range_value_t<TaskRange>, std::ranges::range_reference_t<TaskRange>>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (TaskRange&& rng) COIO_STATIC_CALL_OP_CONST {
				return (*this)(std::ranges::begin(rng), std::ranges::end(rng));
			}
		};

        struct on_sync_wait_final_suspend_fn {
            COIO_STATIC_CALL_OP auto operator()() noexcept ->std::coroutine_handle<> {
                finished_ = 1;
                finished_.notify_all();
                return std::noop_coroutine();
            }
            std::atomic<int> finished_{0};
        };

	    template<typename T>
	    using sync_wait_task = task_wrapper<T, on_sync_wait_final_suspend_fn>;

	    inline constexpr auto do_sync_wait_task = []<typename Awaitable>(Awaitable awaitable) COIO_STATIC_CALL_OP ->sync_wait_task<awaitable_await_result_t<Awaitable>> {
	        if constexpr (std::is_void_v<awaitable_await_result_t<Awaitable>>) {
	            co_await awaitable;
	            co_return;
	        }
	        else co_return co_await awaitable;
	    };

	    struct sync_wait_fn {
	        COIO_STATIC_CALL_OP auto operator() (awaitable auto&& awt) COIO_STATIC_CALL_OP_CONST ->awaitable_await_result_t<decltype(awt)> {
	            auto sync_ = do_sync_wait_task(std::forward<decltype(awt)>(awt));
	            sync_.coro_.resume();
	            sync_.coro_.promise().on_final_suspend_.finished_.wait(0);
                return static_cast<std::add_rvalue_reference_t<awaitable_await_result_t<decltype(awt)>>>(sync_.get_result());
	        }
	    };
	}

	using detail::store_results_in;

	inline constexpr detail::when_all_ready_fn           when_all_ready{};
    inline constexpr detail::when_all_fn                 when_all{};
    inline constexpr detail::make_transform_awaiter_fn   make_transform_awaiter{};
    inline constexpr detail::sync_wait_fn                sync_wait{};
}
