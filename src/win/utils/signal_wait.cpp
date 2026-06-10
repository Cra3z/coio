#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <functional>
#include <mutex>
#include <thread>
#include <coio/utils/atomutex.h>
#include <coio/utils/signal_wait.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep
#include "../common.h"

namespace coio {
    namespace detail {
        namespace {
            constexpr int valid_signals[6]{SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM};

            constexpr std::size_t valid_signal_count = std::size(valid_signals);

            auto signal_index(int signal_number) noexcept -> std::size_t {
                for (std::size_t i = 0; i < valid_signal_count; ++i) {
                    if (signal_number == valid_signals[i])
                        return i;
                }
                return valid_signal_count;
            }
        }

        auto is_available_signal(int signal_number) noexcept -> bool {
            return signal_index(signal_number) < valid_signal_count;
        }

        class signal_state {
        private:
            using node_t = signal_wait_sender::node;

            struct waiting_list {
                auto add(node_t& node) noexcept -> int {
                    std::scoped_lock _{mtx};
                    if (node.stop_requested()) {
                        return -ERROR_OPERATION_ABORTED;
                    }
                    auto old_head = head;
                    if (old_head == nullptr) {
                        return node.signum_;
                    }
                    if (old_head == this and ::signal(node.signum_, &signal_state::signal_handler) == SIG_ERR) {
                        return -errno;
                    }
                    node.prev_ = nullptr;
                    node.next_ = old_head == this ? nullptr : static_cast<node_t*>(old_head);
                    if (node.next_ != nullptr) {
                        node.next_->prev_ = &node;
                    }
                    head = &node;
                    return 0;
                }

                auto remove(node_t& node) noexcept -> bool {
                    std::scoped_lock _{mtx};
                    if (node.prev_ != nullptr) {
                        node.prev_->next_ = node.next_;
                    }
                    else if (head == &node) {
                        head = node.next_;
                        if (head == nullptr) {
                            head = this;
                        }
                    }
                    else {
                        return false;
                    }

                    if (node.next_ != nullptr) {
                        node.next_->prev_ = node.prev_;
                    }

                    node.prev_ = nullptr;
                    node.next_ = nullptr;
                    if (head == this) {
                        static_cast<void>(::signal(node.signum_, SIG_DFL));
                    }
                    return true;
                }

                auto pop_all(int signal_number) noexcept -> void {
                    std::unique_lock guard{mtx};
                    node_t* waiter = head == this ? nullptr : static_cast<node_t*>(head);
                    head = signal_number < 0 ? this : nullptr;
                    while (waiter != nullptr) {
                        auto next = waiter->next_;
                        if (next) next->prev_ = nullptr;
                        waiter->next_ = nullptr;
                        guard.unlock();
                        waiter->finish_(waiter, signal_number);
                        waiter = next;
                        if (waiter != nullptr) {
                            guard.lock();
                        }
                    }
                }

                atomutex mtx;
                void* head = this;
            };

        private:
            signal_state() {
                wake_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (wake_event == nullptr)
                    throw std::system_error{to_error_code(::GetLastError()), "signal_wait"};
                instance = this;
                worker = std::jthread{std::bind_front(&signal_state::watchdog, this)};
            }

        public:
            signal_state(const signal_state&) = delete;

            ~signal_state() {
                worker.request_stop();
                wake_worker();
                worker.join();
                if (wake_event) {
                    ::CloseHandle(wake_event);
                }
            }

            auto operator=(const signal_state&) -> signal_state& = delete;

            auto register_waiter(node_t& node) noexcept -> bool {
                const std::size_t index = signal_index(node.signum_);
                if (const auto ret = waiting_lists[index].add(node); ret != 0) {
                    node.finish_(&node, ret);
                    return false;
                }
                return true;
            }

            auto unregister_waiter(node_t& node) noexcept -> void {
                const std::size_t index = signal_index(node.signum_);
                if (waiting_lists[index].remove(node)) {
                    node.finish_(&node, -ERROR_OPERATION_ABORTED);
                }
            }

        private:
            auto wake_worker() const noexcept -> void {
                ::SetEvent(wake_event);
            }

            auto dispatch_signal(std::size_t index) noexcept -> void {
                const int signal_number = valid_signals[index];
                static_cast<void>(::signal(signal_number, SIG_DFL));
                waiting_lists[index].pop_all(signal_number);
            }

            auto dispatch_pending_signals() noexcept -> void {
                for (std::size_t i = 0; i < valid_signal_count; ++i) {
                    if (not interesting[i].exchange(false, std::memory_order_acq_rel)) continue;
                    dispatch_signal(i);
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
                const std::size_t index = signal_index(signal_number);
                if (index == valid_signal_count)
                    return;
                instance->interesting[index].store(true, std::memory_order_release);
                instance->wake_worker();
            }

        private:
            inline static signal_state* instance = nullptr;
            std::array<waiting_list, valid_signal_count> waiting_lists{};
            std::atomic<bool> interesting[valid_signal_count]{};
            ::HANDLE wake_event = nullptr;
            std::jthread worker;
        };
    }

    auto signal_wait_sender::node::do_start() noexcept -> void {
        detail::signal_state::get().register_waiter(*this);
    }

    auto signal_wait_sender::node::do_cancel() noexcept -> void {
        detail::signal_state::get().unregister_waiter(*this);
    }

    auto strsignal(int signum) noexcept -> std::string_view {
        switch (signum) {
            case SIGINT:
                return "Interrupt";
            case SIGILL:
                return "Illegal instruction";
            case SIGFPE:
                return "Erroneous arithmetic operation";
            case SIGSEGV:
                return "Invalid memory reference";
            case SIGTERM:
                return "Termination request";
            case SIGABRT:
                return "Aborted";
            default:
                return "Unknown signal";
        }
    }
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
