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
#include <stop_token>
#include <variant>
#include <functional>
#include "config.h"
#include "concepts.h"
#include "detail/co_memory.h"
#include "detail/task_error.h"

namespace coio {

	template<awaiter T>
	using await_result_t = decltype(std::declval<T>().await_resume());

	template<awaitable T>
	struct awaitable_traits {
		using awaiter_type = typename detail::get_awaiter<T>::type;

		using await_result_type = ::coio::await_result_t<awaiter_type>;
	};

	struct nothing final {};

	template<typename T = void, typename Alloc = void>
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
		template<typename T>
		using awaitable_await_result_t = typename awaitable_traits<T>::await_result_type;

		template<typename T>
		using awaitable_non_void_await_result_t = std::conditional_t<std::is_void_v<awaitable_await_result_t<T>>, nothing, awaitable_await_result_t<T>>;

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
			static auto await_ready() noexcept -> bool {
				return false;
			}

			auto await_suspend(std::coroutine_handle<> this_coro) const noexcept {
				COIO_ASSERT(coro != nullptr);
				coro.promise().previous_coro_ = this_coro;
				return coro;
			}

			[[nodiscard]]
			auto await_resume() const ->typename TaskType::result_type {
				auto& promise = coro.promise();
				COIO_ASSERT(promise.value_ or promise.except_); // must have a value or error
				if (auto except = std::exchange(promise.except_, {}); except) std::rethrow_exception(except);
				using result_type = typename TaskType::result_type;
				if constexpr (std::is_lvalue_reference_v<result_type>) {
					auto result = std::exchange(promise.value_, nullptr);
					COIO_ASSERT(result != nullptr);
					return *result;
				}
				else {
					auto result = std::move(promise.value_);
					COIO_ASSERT(result.has_value());
					return std::move(*result);
				}
			}

			auto await_resume() const ->void requires std::same_as<typename TaskType::result_type, void> {
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
			friend TaskType;
			friend final_awaiter;
			friend task_promise<TaskType>;
			friend task_awaiter<TaskType>;
		private:

			using allocator_type = typename TaskType::allocator_type;

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

			auto operator new (std::size_t n) ->void* requires std::same_as<allocator_type, void> or std::default_initializable<allocator_type> {
				return co_memory<allocator_type>::allocate(std::conditional_t<std::same_as<allocator_type, void>, std::allocator<void>, allocator_type>(), n);
			}

			template<typename OtherAlloc, typename... Args> requires std::same_as<allocator_type, void> or std::convertible_to<const OtherAlloc&, allocator_type>
			auto operator new (std::size_t n, std::allocator_arg_t, const OtherAlloc& other_alloc, const Args&...) ->void* { // for normal corotuine function `auto some_function(std::allocator_arg_t, allocator, ...) ->coio::task<...>`
				return co_memory<allocator_type>::allocate(other_alloc, n);
			}

			template<typename This, typename OtherAlloc, typename... Args> requires std::same_as<allocator_type, void> or std::convertible_to<const OtherAlloc&, allocator_type>
			auto operator new (std::size_t n, const This&, std::allocator_arg_t, const OtherAlloc& other_alloc, const Args&...) ->void* { // for non-static member corotuine function `auto some_class::some_function(std::allocator_arg_t, allocator, ...) ->coio::task<...>`
				return operator new (n, std::allocator_arg, other_alloc);
			}

			auto operator delete (void* ptr, std::size_t n) noexcept ->void {
				co_memory<allocator_type>::deallocate(ptr, n);
			}

		protected:
			std::coroutine_handle<> previous_coro_;
			std::exception_ptr except_;
		};

		template<typename TaskType>
		struct task_promise : task_promise_base<TaskType> {
			friend task_awaiter<TaskType>;

			auto return_value(typename TaskType::result_type value) noexcept(
				std::is_nothrow_move_constructible_v<typename TaskType::result_type> and
				std::is_nothrow_move_assignable_v<typename TaskType::result_type>
			) -> void {
				value_ = std::move(value);
			}

		private:
			std::optional<typename TaskType::result_type> value_;
		};


		template<typename Alloc>
		struct task_promise<task<void, Alloc>> : task_promise_base<task<void, Alloc>> {
			friend task_awaiter<task<void, Alloc>>;

			auto return_void() noexcept ->void {}
		};

		template<typename T, typename Alloc>
		struct task_promise<task<T&, Alloc>> : task_promise_base<task<T&, Alloc>> {
			friend task_awaiter<task<T&, Alloc>>;

			auto return_value(T& ref) noexcept ->void {
				value_ = std::addressof(ref);
			}

		private:
			T* value_ = nullptr;
		};

	}

	template<typename T, typename Alloc>
	class task {

		friend run_loop;
		friend detail::task_promise_base<task>;
		friend detail::task_promise<task>;

		static_assert((std::is_object_v<T> and std::move_constructible<T>) or std::same_as<T, void> or std::is_lvalue_reference_v<T>);

		using allocator_type = Alloc;

	public:

		using result_type = T;

		using promise_type = detail::task_promise<task>;

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

		auto operator co_await() const -> detail::task_awaiter<task> {
			if (coro == nullptr) throw task_error{task_errc::no_state};
			if (coro.done()) throw task_error{task_errc::already_retrieved};
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

	inline auto detail::sleep_awaiter::await_suspend(std::coroutine_handle<> this_coro) const noexcept -> void {
		run_loop::instance().post_timer(timeout_time, this_coro);
	}

	inline auto run_loop::run_until_complete(task<> co_main) -> void {
		ready_coros.push_back(co_main.coro);
		run();
	}

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

		struct make_transform_awaiter_fn final {
			template<awaiter Awaiter, std::invocable<await_result_t<Awaiter>> Fn> requires std::move_constructible<Awaiter> and std::move_constructible<Fn>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (Awaiter awaiter, Fn fn) COIO_STATIC_CALL_OP_CONST noexcept(std::is_nothrow_move_constructible_v<Awaiter> and std::is_nothrow_move_constructible_v<Fn>) ->transform_awaiter<Awaiter, Fn> {
				return transform_awaiter{std::move(awaiter), std::move(fn)};
			}
		};
		inline constexpr make_transform_awaiter_fn make_transform_awaiter{};

		template<typename T>
		using task_result_helper = std::conditional_t<std::is_void_v<T>, nothing, T>;

		struct when_all_counter final {
			using count_type = std::atomic<std::size_t>;

			explicit when_all_counter(std::size_t count) noexcept : count(count) {
				COIO_ASSERT(count > 0);
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
		using make_ref_wrapper_t = std::conditional_t<std::is_lvalue_reference_v<T>, std::reference_wrapper<std::remove_reference_t<T>>, T>;

		template<typename T>
		struct when_all_task final {

			struct promise_type {

				auto get_return_object() noexcept -> when_all_task {
					return {std::coroutine_handle<promise_type>::from_promise(*this)};
				}

				static auto initial_suspend() noexcept -> std::suspend_always { return {}; }

				auto final_suspend() noexcept -> final_awaiter {
					COIO_ASSERT(counter_ != nullptr);
					return {counter_->decrease()};
				}

				auto return_value(T value) noexcept ->void {
					result_.template emplace<1>(static_cast<T&&>(value));
				}

				auto unhandled_exception() noexcept ->void {
					result_.template emplace<2>(std::current_exception());
				}

				std::variant<std::monostate, make_ref_wrapper_t<T>, std::exception_ptr> result_;
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

			decltype(auto) get_result() const {
				COIO_ASSERT(coro != nullptr and coro.done());
				auto& result = coro.promise().result_;
				if (result.index() == 2) std::rethrow_exception(*std::get_if<2>(&result));
				return static_cast<T&&>(*std::get_if<1>(&result));
			}

			std::coroutine_handle<promise_type> coro;
		};

		template<typename>
		class when_all_ready_awaiter;

		template<elements_move_insertable_range WhenAllTasks> requires std::move_constructible<WhenAllTasks> and specialization_of<std::ranges::range_value_t<WhenAllTasks>, when_all_task>
		class when_all_ready_awaiter<WhenAllTasks> {
		public:
			when_all_ready_awaiter(WhenAllTasks when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)), counter_(std::ranges::distance(when_all_tasks_)) {}

			auto await_ready() const noexcept -> bool {
				return bool(counter_.previous_coro);
			}

			auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
				counter_.previous_coro = this_coro;
				for (auto& task : when_all_tasks_) {
					task.coro.promise().counter_ = &counter_;
					task.coro.resume();
				};
			}

			[[nodiscard]]
			auto await_resume() noexcept(std::is_nothrow_move_constructible_v<WhenAllTasks>) -> WhenAllTasks {
				return std::move(when_all_tasks_);
			}

		private:
			WhenAllTasks when_all_tasks_;
			when_all_counter counter_;
		};

		template<typename... Types>
		class when_all_ready_awaiter<std::tuple<when_all_task<Types>...>> {
		public:

			when_all_ready_awaiter(std::tuple<when_all_task<Types>...> when_all_tasks) : when_all_tasks_(std::move(when_all_tasks)) {}

			auto await_ready() const noexcept -> bool {
				return bool(counter_.previous_coro);
			}

			auto await_suspend(std::coroutine_handle<> this_coro) noexcept -> void {
				counter_.previous_coro = this_coro;
				[this]<std::size_t... I>(std::index_sequence<I...>) {
					(..., (std::get<I>(when_all_tasks_).coro.promise().counter_ = &counter_));
					(..., std::get<I>(when_all_tasks_).coro.resume());
				}(std::make_index_sequence<sizeof...(Types)>{});
			}

			[[nodiscard]]
			auto await_resume() noexcept(std::is_nothrow_move_constructible_v<std::tuple<when_all_task<Types>...>>) -> std::tuple<when_all_task<Types>...> {
				return std::move(when_all_tasks_);
			}

		private:
			std::tuple<when_all_task<Types>...> when_all_tasks_;
			when_all_counter counter_{sizeof...(Types)};
		};

		template<typename... Types>
		when_all_ready_awaiter(std::tuple<when_all_task<Types>...>) -> when_all_ready_awaiter<std::tuple<when_all_task<Types>...>>;


		template<typename Awaitable, typename AwaitResultType = awaitable_await_result_t<Awaitable>>
		auto do_when_all_task(Awaitable awaitable) ->when_all_task<awaitable_non_void_await_result_t<Awaitable>> {
			if constexpr (std::is_void_v<AwaitResultType>) {
				co_await awaitable;
				co_return nothing{};
			}
			else co_return co_await awaitable;
		}

		template<typename ResultStorageRange>
		struct store_results_in_t final {};

		template<unqualified_object TaskStorageRange>
		inline constexpr store_results_in_t<TaskStorageRange> store_results_in{};

		struct when_all_ready_fn final {
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
						awaitable_non_void_await_result_t<std::iter_value_t<It>>
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
		inline constexpr when_all_ready_fn when_all_ready{};


		struct when_all_fn final {
			template<awaitable... Awaitables>
			[[nodiscard]]
			COIO_STATIC_CALL_OP auto operator() (Awaitables&&... awaitables) COIO_STATIC_CALL_OP_CONST requires (sizeof...(awaitables) > 0) and (... and std::constructible_from<std::decay_t<Awaitables>, Awaitables&&>) {
				return transform_awaiter{
					when_all_ready(std::forward<Awaitables>(awaitables)...),
					[]<typename... Types>(std::tuple<when_all_task<Types>...> when_all_tasks_) {
						return [&when_all_tasks_]<std::size_t... I>(std::index_sequence<I...>) {
							return std::tuple<Types...>{std::get<I>(when_all_tasks_).get_result()...};
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
					when_all_ready(first, last),
					[]<typename T>(std::vector<when_all_task<T>> when_all_tasks_) {
						ResultStorageRange result_storage_range;
						for (auto& task : when_all_tasks_) {
							void(result_storage_range.insert(std::ranges::end(result_storage_range), task.get_result()));
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
		inline constexpr when_all_fn when_all{};
	}

	using detail::make_transform_awaiter;
	using detail::when_all_task;
	using detail::when_all_ready;
	using detail::when_all;
	using detail::store_results_in;

}
