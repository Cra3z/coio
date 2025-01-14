#pragma once
#include <atomic>
#include <chrono>
#include <coroutine>

namespace coio {
    class io_context;

    class steady_timer {
    public:
        using clock_type = std::chrono::steady_clock;
        using duration = clock_type::duration;
        using time_point = clock_type::time_point;

    public:
        struct awaiter {
            awaiter(io_context& context, time_point deadline) noexcept : context_(context), deadline_(deadline) {
                context_.work_started();
            }

            ~awaiter() {
                context_.work_finished();
            }

            auto await_ready() noexcept -> bool {
                return cancelled_.load() or deadline_ <= clock_type::now();
            }

            auto await_suspend(std::coroutine_handle<> this_coro) -> void;

            static auto await_resume() noexcept -> void {}

            auto cancel() noexcept -> void {
                cancelled_.store(true);
            }

            io_context& context_;
            time_point deadline_;
            std::coroutine_handle<> coro_;
            std::atomic<bool> cancelled_{false};
        };
    public:
        explicit steady_timer(io_context& context) noexcept : context_(&context) {}

        steady_timer(io_context& context, std::chrono::steady_clock::time_point timeout) noexcept : context_(&context), deadline_(timeout) {}

        steady_timer(io_context& context, std::chrono::steady_clock::duration duration) noexcept : steady_timer(context, clock_type::now() + duration) {}

        steady_timer(steady_timer&& other) noexcept : context_(other.context_), deadline_(std::exchange(other.deadline_, {})) {}

        auto operator= (steady_timer other) noexcept -> steady_timer& {
            std::swap(other.deadline_, deadline_);
            std::swap(other.context_, context_);
            return *this;
        }

        [[nodiscard]]
        auto async_wait() noexcept -> awaiter {
            return {*context_, deadline_};
        }

        [[nodiscard]]
        auto context() const noexcept -> io_context& {
            return *context_;
        }
    private:
    private:
        io_context* context_;
        time_point deadline_;
    };
}