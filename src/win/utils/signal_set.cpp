#include <array>
#include <atomic>
#include <cerrno>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <coio/utils/signal_set.h>
#include "../common.h"

namespace coio {
    namespace detail {
        namespace {
            constexpr int valid_signals[6]{SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM};

            constexpr std::size_t valid_signal_count = std::ranges::size(valid_signals);
        }

        class signal_state {
            friend signal_set;
        private:
            signal_state() {
                wake_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (wake_event == nullptr) throw std::system_error{to_error_code(::GetLastError()), "signal_set"};
                instance = this;
                worker = std::jthread{std::bind_front(&signal_state::watchdog, this)};
            }

        public:
            signal_state(const signal_state&) = delete;

            ~signal_state() {
                worker.request_stop();
                wake_worker();
                worker.join();
                if (wake_event) ::CloseHandle(wake_event);
            }

            auto operator= (const signal_state&) -> signal_state& = delete;

        private:
            auto wake_worker() const noexcept -> void {
                ::SetEvent(wake_event);
            }

            auto dispatch_pending_signals() -> void {
                for (std::size_t i = 0; i < valid_signal_count; ++i) {
                    if (not interesting[i].exchange(false, std::memory_order_acq_rel)) continue;

                    const int signal_number = valid_signals[i];
                    std::unordered_set<signal_set*> sigsets;
                    {
                        std::unique_lock lck{mutex};
                        sigsets = sigset_table[i];
                    }

                    for (auto sigset : sigsets) {
                        auto node = sigset->listeners_.pop_all();
                        while (node) {
                            const auto next = node->next_;
                            node->finish_(node, signal_number);
                            node = next;
                        }
                    }
                }
            }

            // ReSharper disable once CppPassValueParameterByConstReference
            auto watchdog(std::stop_token stop_token) -> void { // NOLINT(*-unnecessary-value-param)
                while (not stop_token.stop_requested()) {
                    ::WaitForSingleObject(wake_event, INFINITE);
                    dispatch_pending_signals();
                }
            }

        public:
            static auto get() -> signal_state& {
                static signal_state state{};
                return state;
            }

            static auto signal_handler(int signal_number) -> void {
                for (std::size_t i = 0; i < valid_signal_count; ++i) {
                    if (valid_signals[i] != signal_number) continue;
                    instance->interesting[i].store(true, std::memory_order_release);
                    instance->wake_worker();
                    break;
                }
            }

        private:
            inline static signal_state* instance = nullptr;
            std::mutex mutex;
            std::unordered_set<signal_set*> sigset_table[valid_signal_count]{};
            std::atomic<bool> interesting[valid_signal_count]{};
            ::HANDLE wake_event = nullptr;
            std::jthread worker;
        };
    }

    signal_set::~signal_set() {
        clear();
    }

    auto signal_set::add(int signal_number) -> void {
        std::size_t signal_index = detail::valid_signal_count;
        for (std::size_t i = 0; i < detail::valid_signal_count; ++i) {
            if (signal_number == detail::valid_signals[i]) {
                signal_index = i;
                break;
            }
        }

        if (signal_index == detail::valid_signal_count) {
            throw std::invalid_argument{"invalid signal number"};
        }

        if (signal_numbers_.contains(signal_number)) return;

        auto& state = detail::signal_state::get();
        bool need_register = false;
        {
            std::scoped_lock _{state.mutex};
            need_register = state.sigset_table[signal_index].empty();
            state.sigset_table[signal_index].emplace(this);
        }
        if (need_register) {
            if (::signal(signal_number, &detail::signal_state::signal_handler) == SIG_ERR) {
                throw std::system_error{std::error_code(errno, std::system_category())};
            }
        }

        signal_numbers_.emplace(signal_number);
    }

    auto signal_set::remove(int signal_number) -> void {
        std::size_t signal_index = detail::valid_signal_count;
        for (std::size_t i = 0; i < detail::valid_signal_count; ++i) {
            if (signal_number == detail::valid_signals[i]) {
                signal_index = i;
                break;
            }
        }

        if (signal_index == detail::valid_signal_count) {
            throw std::invalid_argument{"invalid signal number"};
        }

        if (not signal_numbers_.contains(signal_number)) return;

        auto& state = detail::signal_state::get();
        bool need_unregister = false;
        {
            std::scoped_lock _{state.mutex};
            need_unregister = state.sigset_table[signal_index].size() == 1;
            if (state.sigset_table[signal_index].erase(this) == 0) return;
        }
        if (need_unregister) {
            if (::signal(signal_number, SIG_DFL) == SIG_ERR) {
                throw std::system_error{std::error_code(errno, std::system_category())};
            }
        }

        signal_numbers_.erase(signal_number);
        if (signal_numbers_.empty()) cancel();
    }

    auto signal_set::cancel() -> void {
        auto node = listeners_.pop_all();
        while (node) {
            const auto next = node->next_;
            node->finish_(node, -ERROR_OPERATION_ABORTED);
            node = next;
        }
    }

    auto signal_set::clear() -> void {
        auto numbers = signal_numbers_;
        for (auto signal_number : numbers) remove(signal_number);
    }

    auto strsignal(int signum) noexcept -> std::string_view {
        switch (signum) {
        case SIGINT:  return "Interrupt";
        case SIGILL:  return "Illegal instruction";
        case SIGFPE:  return "Erroneous arithmetic operation";
        case SIGSEGV: return "Invalid memory reference";
        case SIGTERM: return "Termination request";
        case SIGABRT: return "Aborted";
        default:      return "Unknown signal";
        }
    }
}