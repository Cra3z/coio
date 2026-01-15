#include <thread>
#include <unordered_set>
#include <fcntl.h>
#include <poll.h>
#include <coio/utils/signal_set.h>
#include "../common.h"

namespace coio {
    namespace detail {
        namespace {
#if defined(NSIG) && (NSIG > 0)
            constexpr int max_signal_number = NSIG;
#else
            constexpr int max_signal_number = 65;
#endif

            auto is_valid_signal_number(int signal_number) noexcept -> bool {
                return signal_number >= 0 and signal_number < max_signal_number;
            }
        }

        class signal_state {
            friend signal_set;
        private:
            signal_state() {
                int pipedes[2];
                detail::throw_last_error(::pipe2(pipedes, O_CLOEXEC | O_NONBLOCK));
                watcher = pipedes[0];
                notifier = pipedes[1];
                instance = this;
                worker = std::jthread{std::bind_front(&signal_state::watchdog, this)};
            }

        public:
            signal_state(const signal_state&) = delete;

            ~signal_state() {
                worker.request_stop();
                constexpr int dummy_signal = -1; // -1 isn't a valid signal number, just to wake up the watchdog
                ::write(notifier, &dummy_signal, sizeof(dummy_signal));
                worker.join();
                no_errno_here(::close(watcher));
                no_errno_here(::close(notifier));
            }

            auto operator= (const signal_state&) -> signal_state& = delete;

        private:
            // ReSharper disable once CppPassValueParameterByConstReference
            auto watchdog(std::stop_token stop_token) -> void { // NOLINT(*-unnecessary-value-param)
                ::pollfd pfd{
                    .fd = watcher,
                    .events = POLLIN | POLLERR
                };
                while (not stop_token.stop_requested()) {
                    if (::poll(&pfd, 1, -1) != 1) continue;
                    int signal_number;
                    const auto n = ::read(watcher, &signal_number, sizeof(signal_number));
                    if (n != sizeof(signal_number) or not is_valid_signal_number(signal_number)) continue;
                    std::unique_lock lck{mutex};
                    auto sigsets = sigset_table[signal_number];  // copy, not move
                    lck.unlock();
                    for (auto sigset : sigsets) {
                        auto node = sigset->awaiters_.pop_all();
                        while (node) {
                            const auto next = node->next_;
                            node->signal_number_ = signal_number;
                            node->coro_.resume();
                            node = next;
                        }
                    }
                }
            }

        public:
            static auto get() -> signal_state& {
                static signal_state state{};
                return state;
            }

            static auto signal_handler(int signal_number) -> void {
                const int prev_errno = errno;
                ::write(instance->notifier, &signal_number, sizeof(signal_number));
                errno = prev_errno;
            }

        private:
            inline static signal_state* instance = nullptr;
            int watcher = -1;
            int notifier = -1;
            std::mutex mutex;
            std::unordered_set<signal_set*> sigset_table[max_signal_number]{};
            std::jthread worker;
        };
    }

    signal_set::~signal_set() {
        clear();
    }

    auto signal_set::add(int signal_number) -> void {
        if (not detail::is_valid_signal_number(signal_number)) {
            throw std::invalid_argument{"invalid signal number"};
        }

        if (signal_numbers_.contains(signal_number)) return;

        auto& state = detail::signal_state::get();
        bool need_register = false;
        {
            std::scoped_lock _{state.mutex};
            need_register = state.sigset_table[signal_number].empty();
            state.sigset_table[signal_number].emplace(this);
        }
        if (need_register) {
            struct ::sigaction sa{};
            sa.sa_handler = detail::signal_state::signal_handler;
            ::sigfillset(&sa.sa_mask);
            if (::sigaction(signal_number, &sa, nullptr) == -1) {
                throw std::system_error{std::error_code(errno,std::system_category())};
            }
        }

        signal_numbers_.emplace(signal_number);
    }

    auto signal_set::remove(int signal_number) -> void {
        if (not detail::is_valid_signal_number(signal_number)) {
            throw std::invalid_argument{"invalid signal number"};
        }

        if (not signal_numbers_.contains(signal_number)) return;

        auto& state = detail::signal_state::get();
        bool need_unregister = false;
        {
            std::scoped_lock _{state.mutex};
            need_unregister = state.sigset_table[signal_number].size() == 1;
            if (state.sigset_table[signal_number].erase(this) == 0) return;
        }
        if (need_unregister) {
            struct ::sigaction sa{};
            sa.sa_handler = SIG_DFL;
            ::sigfillset(&sa.sa_mask);
            if (::sigaction(signal_number, &sa, nullptr) == -1) {
                throw std::system_error{std::error_code(errno,std::system_category())};
            }
        }

        signal_numbers_.erase(signal_number);
        if (signal_numbers_.empty()) cancel();
    }

    auto signal_set::cancel() -> void {
        auto node = awaiters_.pop_all();
        while (node) {
            const auto next = node->next_;
            node->unhandled_stopped_(node->coro_).resume();
            node = next;
        }
    }

    auto signal_set::clear() -> void {
        auto numbers = signal_numbers_;
        for (auto signal_number : numbers) remove(signal_number);
    }
}