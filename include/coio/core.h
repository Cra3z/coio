#pragma once
#include <coroutine>
#include <cassert>
#include <deque>
#include <vector>
#include <ranges>
#include <queue>
#include <chrono>
#include <mutex>
#include <atomic>
#include <future>
#include <stop_token>
#include <variant>
#include "config.h"
#include "concepts.h"
#include "task_error.h"

namespace coio {

	template<awaiter T>
	using await_result_t = decltype(std::declval<T>().await_resume());

	template<awaitable T>
	struct awaitable_traits {
		using awaiter_type = typename detail::get_awaiter<T>::type;

		using await_result_type = ::coio::await_result_t<awaiter_type>;
	};

	template<typename T = void>
	class [[nodiscard]] task;

	class run_loop {
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

		[[nodiscard]]
		static auto instance() ->run_loop& {
			static run_loop instance_{};
			return instance_;
		}

		run_loop() = default;

		run_loop(const run_loop&) = delete;

		auto operator= (const run_loop&) -> run_loop& = delete;

		auto post_timer(std::chrono::steady_clock::time_point timeout_time, std::coroutine_handle<> coro) ->void {
			std::scoped_lock lock{mtx_};
			sleeping_coros.emplace(timeout_time, coro);
		}

		auto post(std::coroutine_handle<> coro) ->void {
			std::scoped_lock lock{mtx_};
			ready_coros.push_back(coro);
		}

		auto post_wait(std::coroutine_handle<> coro) ->void {}

		auto run_until_complete(task<> co_main) ->void;

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

				if (sleeping_coros.empty()) break;
			}
		}

		auto stop() noexcept ->void {
			stop_.request_stop();
		}

		[[nodiscard]]
		auto stopped() const noexcept -> bool {
			return stop_.stop_requested();
		}

	private:
		std::recursive_mutex mtx_;
		std::stop_source stop_;
		std::deque<std::coroutine_handle<>> ready_coros;
		std::priority_queue<timed_coro, std::vector<timed_coro>, std::greater<>> sleeping_coros;
	};



	namespace detail {
		struct final_awaiter {
			static auto await_ready() noexcept -> bool { return false; }

			auto await_suspend(std::coroutine_handle<>) const noexcept -> std::coroutine_handle<> {
				if (previous_coro) return previous_coro;
				return std::noop_coroutine();
			}

			static auto await_resume() noexcept ->void {}

			std::coroutine_handle<> previous_coro;
		};

		template<typename TaskType>
		struct task_awaiter {
			auto await_ready() noexcept -> bool { // assume: 若`coro`非空则`coro`指代的协程一定在最终暂停点暂停
				return coro == nullptr or coro.done();
			}

			auto await_suspend(std::coroutine_handle<> this_coro) const noexcept {
				coro.promise().previous_coro_ = this_coro;
				return coro;
			}

			[[nodiscard]]
			auto await_resume() const ->typename TaskType::result_type {
				auto& promise = coro.promise();
				assert(promise.value_ or promise.except_); // must have a value or error
				if (auto except = std::exchange(promise.except_, {}); except) std::rethrow_exception(except);
				auto result = std::move(promise.value_);
				return std::move(result.value());
			}

			auto await_resume() const noexcept ->void requires std::same_as<TaskType, task<>> {
				auto& promise = coro.promise();
				if (auto except = std::exchange(promise.except_, {}); except) std::rethrow_exception(except);
			}

			std::coroutine_handle<typename TaskType::promise_type> coro;
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
		struct task_promise;

		template<typename TaskType>
		class task_promise_base {
			friend task_promise<TaskType>;
			friend final_awaiter;
		private:

			task_promise_base() = default;

		public:

			auto get_return_object() noexcept ->TaskType {
				return {std::coroutine_handle<task_promise<TaskType>>::from_promise(*static_cast<task_promise<TaskType>*>(this))};
			}

			auto initial_suspend() noexcept -> std::suspend_always { return {}; }

			auto final_suspend() noexcept -> final_awaiter { return {previous_coro_}; }

			auto unhandled_exception() noexcept -> void {
				except_ = std::current_exception();
			}

			template<typename Rep, typename Period>
			auto await_transform(std::chrono::duration<Rep, Period> duration) noexcept -> sleep_awaiter {
				return {.timeout_time = std::chrono::steady_clock::now() + duration};
			}

			decltype(auto) await_transform(awaitable_for<task_promise<TaskType>> auto&& awt) noexcept { /// identity
				return std::forward<decltype(awt)>(awt);
			}

		protected:
			std::coroutine_handle<> previous_coro_;
			std::exception_ptr except_;
		};


		template<typename>
		struct task_base;

		template<typename TaskType>
		struct task_promise : task_promise_base<TaskType> {
			friend task_awaiter<TaskType>;
			friend task_base<TaskType>;

			auto return_value(typename TaskType::result_type value) noexcept(
				std::is_nothrow_move_constructible_v<typename TaskType::result_type> and
				std::is_nothrow_move_assignable_v<typename TaskType::result_type>
			) -> void {
				value_ = std::move(value);
			}

		private:
			std::optional<typename TaskType::result_type> value_;
		};


		template<>
		struct task_promise<task<>> : task_promise_base<task<>> {
			friend task_awaiter<task<>>;
			friend task_base<task<>>;

			auto return_void() noexcept ->void {}
		};


		template<typename T>
		struct task_base {
			static_assert(std::is_object_v<T> and not std::is_array_v<T> and std::movable<T>, "type `T` must be movable non-array object type.");

			using result_type = T;

			using promise_type = task_promise<task<T>>;
		};

		template<typename T>
		struct task_base<T&> {

			using result_type = T&;

			using promise_type = task_promise<task<std::reference_wrapper<T>>>;

		};

		template<>
		struct task_base<void> {

			using result_type = void;

			using promise_type = task_promise<task<>>;

		};

		template<typename T>
		auto handle_of_task(const task<T>&) noexcept ->std::coroutine_handle<>;
	}

	template<typename T>
	class task : detail::task_base<T> {

		friend run_loop;
		friend detail::task_promise_base<task>;
		friend detail::task_promise<task>;
		friend auto detail::handle_of_task<T>(const task&) noexcept ->std::coroutine_handle<>;

		using base = detail::task_base<T>;

	public:

		using typename base::result_type;

		using typename base::promise_type;

	private:

		task(std::coroutine_handle<promise_type> coro) noexcept : coro{coro} {}

	public:

		task() = default;

		task(const task&) = delete;

		task(task&& other) noexcept : coro{std::exchange(other.coro, {})} {}

		~task() {
			if (coro) coro.destroy();
		}

		auto operator= (const task&) -> task& = delete;

		auto operator= (task&& other) noexcept -> task& {
			task(std::move(other)).swap(*this);
			return *this;
		}

		auto swap(task& other) noexcept ->void {
			std::swap(coro, other.coro);
		}

		friend auto swap(task& lhs, task& rhs) noexcept -> void {
			lhs.swap(rhs);
		}

		auto operator co_await() const noexcept -> detail::task_awaiter<task> {
			return {.coro = coro};
		}

		[[nodiscard]]
		auto ready() const noexcept ->bool {
			return coro == nullptr or coro.done();
		}

		auto clear() noexcept ->void {
			task{}.swap(*this);
		}

	private:
		std::coroutine_handle<promise_type> coro;
	};

	template<typename T>
	auto detail::handle_of_task(const task<T>& task_) noexcept ->std::coroutine_handle<> {
		return task_.coro;
	}

	inline auto detail::sleep_awaiter::await_suspend(std::coroutine_handle<> this_coro) const noexcept -> void {
		run_loop::instance().post_timer(timeout_time, this_coro);
	}

	inline auto run_loop::run_until_complete(task<> co_main) -> void {
		ready_coros.push_back(co_main.coro);
		run();
	}

	struct nothing final {};

	template<typename Awaiter, typename Fn>
	struct awaiter_transformer {

		static_assert(awaiter<Awaiter> and std::move_constructible<Awaiter>);

		static_assert(std::invocable<Fn, await_result_t<Awaiter>> and std::move_constructible<Fn>);

		awaiter_transformer(Awaiter awaiter, Fn fn) noexcept(std::is_nothrow_move_constructible_v<Awaiter> and std::is_nothrow_move_constructible_v<Fn>) : inner_awaiter_(std::move(awaiter)), fn_(std::move(fn)) {}

		auto await_ready() noexcept(noexcept(std::declval<Awaiter&>().await_ready())) {
			return inner_awaiter_.await_ready();
		}

		auto await_suspend(std::coroutine_handle<> this_coro) noexcept(noexcept(std::declval<Awaiter&>().await_suspend(std::declval<std::coroutine_handle<>>()))) {
			return inner_awaiter_.await_suspend(this_coro);
		}

		decltype(auto) await_resume() noexcept(noexcept(std::invoke(std::declval<Fn>(), std::declval<Awaiter&>().await_resume()))) {
			return std::invoke(fn_, inner_awaiter_.await_resume());
		}

	private:
		Awaiter inner_awaiter_;
		Fn fn_;
	};

	namespace detail {

		struct transform_awaiter_fn final {
			template<awaiter Awaiter, std::invocable<await_result_t<Awaiter>> Fn> requires std::move_constructible<Awaiter> and std::move_constructible<Fn>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (Awaiter awaiter, Fn fn) COIO_STATIC_CALL_OP_CONST noexcept(std::is_nothrow_move_constructible_v<Awaiter> and std::is_nothrow_move_constructible_v<Fn>) ->awaiter_transformer<Awaiter, Fn> {
				return awaiter_transformer{std::move(awaiter), std::move(fn)};
			}
		};
		inline constexpr transform_awaiter_fn transform_awaiter{};

		template<typename T>
		using task_result_helper = std::conditional_t<std::is_void_v<T>, nothing, T>;

		struct when_all_counter final {
			using count_type = std::atomic<std::size_t>;

			explicit when_all_counter(std::size_t count) noexcept : count(count) {
				assert(count > 0);
			}

			[[nodiscard]]
			auto decrease() noexcept ->std::coroutine_handle<> {
				if (--count == 0) return previous_coro;
				return std::noop_coroutine();
			}

			count_type count;
			std::coroutine_handle<> previous_coro;
		};

		template<typename T>
		struct when_all_task final {

			struct promise_type {

				auto get_return_object() noexcept -> when_all_task {
					return {std::coroutine_handle<promise_type>::from_promise(*this)};
				}

				static auto initial_suspend() noexcept -> std::suspend_always { return {}; }

				auto final_suspend() noexcept -> final_awaiter {
					assert(counter_ != nullptr);
					return {counter_->decrease()};
				}

				auto return_value(T value) noexcept ->void {
					value_ = std::move(value);
				}

				static auto unhandled_exception() ->void {
					throw;
				}

				std::optional<T> value_;
				when_all_counter* counter_ = nullptr;

			};

			when_all_task(std::coroutine_handle<promise_type> coro) noexcept : coro(coro) {}

			when_all_task(const when_all_task&) = delete;

			when_all_task(when_all_task&& other) noexcept : coro(std::exchange(other.coro, {})) {}

			~when_all_task() {
				if (coro) coro.destroy();
			}

			auto operator= (const when_all_task&) = delete;

			auto operator= (when_all_task&& other) noexcept -> when_all_task& {
				when_all_task(std::move(other)).swap(*this);
				return *this;
			}

			auto swap(when_all_task& other) noexcept ->void {
				std::swap(coro, other.coro);
			}

			friend auto swap(when_all_task& lhs, when_all_task& rhs) noexcept ->void {
				lhs.swap(rhs);
			}

			/** \note: 保证任务完成后才调用
			 */
			auto get_result() const {
				assert(coro.promise().value_.has_value());
				return std::move(*coro.promise().value_);
			}

			std::coroutine_handle<promise_type> coro;
		};

		template<typename>
		class when_all_ready_awaiter;

		template<typename... Types>
		class when_all_ready_awaiter<std::tuple<when_all_task<Types>...>> {
		public:

			when_all_ready_awaiter(std::tuple<when_all_task<Types>...> when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)) {}

			auto await_ready() const noexcept -> bool {
				return bool(counter_->previous_coro);
			}

			auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
				counter_->previous_coro = this_coro;
				[this]<std::size_t... I>(std::index_sequence<I...>) {
					(..., (std::get<I>(when_all_tasks_).coro.promise().counter_ = counter_.get()));
					(..., std::get<I>(when_all_tasks_).coro.resume());
				}(std::make_index_sequence<sizeof...(Types)>{});
			}

			[[nodiscard]]
			auto await_resume() noexcept(std::is_nothrow_move_constructible_v<std::tuple<when_all_task<Types>...>>) -> std::tuple<when_all_task<Types>...> {
				return std::move(when_all_tasks_);
			}

		private:
			std::tuple<when_all_task<Types>...> when_all_tasks_;

			std::unique_ptr<when_all_counter> counter_{std::make_unique<when_all_counter>(sizeof...(Types))};
		};

		template<typename... Types>
		when_all_ready_awaiter(std::tuple<when_all_task<Types>...>) -> when_all_ready_awaiter<std::tuple<when_all_task<Types>...>>;


		template<typename Awaitable, typename AwaitResultType = typename awaitable_traits<std::remove_reference_t<Awaitable>>::await_result_type>
		auto do_when_all_task(Awaitable&& awaitable) ->when_all_task<std::conditional_t<std::is_void_v<AwaitResultType>, nothing, AwaitResultType>> {
			if constexpr (std::is_void_v<AwaitResultType>) {
				co_await std::forward<Awaitable>(awaitable);
				co_return nothing{};
			}
			else co_return co_await std::forward<Awaitable>(awaitable);
		}


		struct when_all_ready_fn final {
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (awaitable auto&&... awaitables) COIO_STATIC_CALL_OP_CONST {
				return when_all_ready_awaiter{
					std::tuple{
						do_when_all_task(std::forward<decltype(awaitables)>(awaitables))...
					}
				};
			}
		};
		inline constexpr when_all_ready_fn when_all_ready{};


		struct when_all_fn final {
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (awaitable auto&&... awaitables) COIO_STATIC_CALL_OP_CONST {
				return transform_awaiter(
					when_all_ready(std::forward<decltype(awaitables)>(awaitables)...),
					[]<typename... Types>(std::tuple<when_all_task<Types>...> when_all_tasks_) {
						return [&when_all_tasks_]<std::size_t... I>(std::index_sequence<I...>) {
							return std::tuple{std::get<I>(when_all_tasks_).get_result()...};
						}(std::make_index_sequence<sizeof...(Types)>{});
					}
				);
			}
		};
		inline constexpr when_all_fn when_all{};
	}

	using detail::transform_awaiter;
	using detail::when_all_ready;
	using detail::when_all;

}
