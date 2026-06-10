#include <array>
#include <cerrno>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <coio/utils/atomutex.h>
#include <coio/utils/signal_wait.h>
#include <coio/detail/suppress_push.h> // IWYU pragma: keep
#include "../common.h"

namespace coio {
    namespace detail {
        namespace {
#if defined(NSIG) && (NSIG > 0)
            constexpr int max_signal_number = NSIG;
#else
            constexpr int max_signal_number = 65;
#endif
        }

        auto is_available_signal(int signal_number) noexcept -> bool {
            return signal_number >= 0 and signal_number < max_signal_number;
        }

        class signal_state {
        private:
            using node_t = signal_wait_sender::node;

            struct waiting_list {
                auto add(node_t& node) noexcept -> int {
                    std::scoped_lock _{mtx};
                    if (node.stop_requested()) {
                        return -ECANCELED;
                    }
                    auto old_head = head;
                    if (old_head == nullptr) {
                        return node.signum_;
                    }
                    if (old_head == this) {
                        struct ::sigaction sa{};
                        sa.sa_handler = signal_handler;
                        ::sigfillset(&sa.sa_mask);
                        if (::sigaction(node.signum_, &sa, nullptr) == -1) {
                            return -errno;
                        }
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
                        struct ::sigaction sa{};
                        sa.sa_handler = SIG_DFL;
                        ::sigfillset(&sa.sa_mask);
                        static_cast<void>(::sigaction(node.signum_, &sa, nullptr));
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
                static_cast<void>(::write(notifier, &dummy_signal, sizeof(dummy_signal)));
                worker.join();
                no_errno_here(::close(watcher));
                no_errno_here(::close(notifier));
            }

            auto operator= (const signal_state&) -> signal_state& = delete;

            auto register_waiter(node_t& node) noexcept -> bool {
                auto& list = waiting_lists[node.signum_];
                if (const auto ret = list.add(node); ret != 0) {
                    node.finish_(&node, ret);
                    return false;
                }
                return true;
            }

            auto unregister_waiter(node_t& node) noexcept -> void {
                if (waiting_lists[node.signum_].remove(node)) {
                    node.finish_(&node, -ECANCELED);
                }
            }

        private:
            auto dispatch_signal(int signal_number) noexcept -> void {
                auto& list = waiting_lists[signal_number];
                struct ::sigaction sa{};
                sa.sa_handler = SIG_DFL;
                ::sigfillset(&sa.sa_mask);
                static_cast<void>(::sigaction(signal_number, &sa, nullptr));
                list.pop_all(signal_number);
            }

            // ReSharper disable once CppPassValueParameterByConstReference
            auto watchdog(std::stop_token stop_token) -> void { // NOLINT(*-unnecessary-value-param)
                ::pollfd pfd{.fd = watcher, .events = POLLIN | POLLERR};
                while (not stop_token.stop_requested()) {
                    if (::poll(&pfd, 1, -1) != 1) continue;
                    int signal_number;
                    const auto n = ::read(watcher, &signal_number, sizeof(signal_number));
                    if (n != sizeof(signal_number) or not is_available_signal(signal_number)) continue;
                    dispatch_signal(signal_number);
                }
            }

        public:
            static auto get() -> signal_state& {
                static signal_state state{};
                return state;
            }

            static auto signal_handler(int signal_number) -> void {
                const int prev_errno = errno;
                static_cast<void>(::write(instance->notifier, &signal_number, sizeof(signal_number)));
                errno = prev_errno;
            }

        private:
            inline static signal_state* instance = nullptr;
            int watcher = -1;
            int notifier = -1;
            std::array<waiting_list, max_signal_number> waiting_lists{};
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
        return ::strsignal(signum);
    }
}

#include <coio/detail/suppress_pop.h> // IWYU pragma: keep
