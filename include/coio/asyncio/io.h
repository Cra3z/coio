// ReSharper disable CppRedundantTypenameKeyword
#pragma once
#include <algorithm>
#include <cstring>
#include <span>
#include <stop_token>  // IWYU pragma: keep
#include <string_view>
#include "coio/detail/async_result.h"
#include "../core.h"
#include "../detail/error.h" //  IWYU pragma: keep

namespace coio {
    template<typename T>
    concept input_stream_device = requires (T t, std::span<std::byte> buffer) {
        { t.read_some(buffer) } -> std::integral;
    };

    template<typename T>
    concept output_stream_device = requires (T t, std::span<const std::byte> buffer) {
        { t.write_some(buffer) } -> std::integral;
    };

    template<typename T>
    concept input_random_access_device = requires (T t, std::size_t offset, std::span<std::byte> buffer) {
        { t.read_some_at(offset, buffer) } -> std::integral;
    };

    template<typename T>
    concept output_random_access_device = requires (T t, std::size_t offset, std::span<const std::byte> buffer) {
        { t.write_some_at(offset, buffer) } -> std::integral;
    };

    template<typename T>
    concept stream_device = input_stream_device<T> and output_stream_device<T>;

    template<typename T>
    concept random_access_device = input_random_access_device<T> and output_random_access_device<T>;

    template<typename T>
    concept async_input_stream_device = requires (T t, std::span<std::byte> buffer) {
        { t.async_read_some(buffer) } -> execution::sender;
    };

    template<typename T>
    concept async_output_stream_device = requires (T t, std::span<const std::byte> buffer) {
        { t.async_write_some(buffer) } -> execution::sender;
    };

    template<typename T>
    concept async_input_random_access_device = requires (T t, std::size_t offset, std::span<std::byte> buffer) {
        { t.async_read_some_at(offset, buffer) } -> execution::sender;
    };

    template<typename T>
    concept async_output_random_access_device = requires (T t, std::size_t offset, std::span<const std::byte> buffer) {
        { t.async_write_some_at(offset, buffer) } -> execution::sender;
    };

    template<typename T>
    concept async_stream_device = async_input_stream_device<T> and async_output_stream_device<T>;

    template<typename T>
    concept async_random_access_device = async_input_random_access_device<T> and async_output_random_access_device<T>;

    template<typename T>
    concept dynamic_buffer = requires (T t, const T& ct, std::size_t n) {
        { ct.size() } -> std::integral;
        { ct.capacity() } -> std::integral;
        { ct.max_size() } -> std::integral;
        { ct.data() } -> std::convertible_to<std::span<const std::byte>>;
        { t.prepare(n) } -> std::convertible_to<std::span<std::byte>>;
        t.commit(n);
        t.consume(n);
    };

    namespace detail {
        struct read_t {
            COIO_STATIC_CALL_OP auto operator() (
                input_stream_device auto& device,
                std::span<std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t total = buffer.size();
                if (total == 0) return 0;
                std::size_t remain = total;
                do {
                    remain -= device.read_some(buffer.subspan(total - remain, remain));
                }
                while (remain > 0);
                return total;
            }

            COIO_STATIC_CALL_OP auto operator() (
                input_stream_device auto& device,
                dynamic_buffer auto& dyn_buffer,
                std::size_t total
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                std::size_t bytes_transferred = read_t{}(device, dyn_buffer.prepare(total));
                COIO_ASSERT(bytes_transferred == total);
                dyn_buffer.commit(bytes_transferred);
                return total;
            }
        };

        struct write_t {
            COIO_STATIC_CALL_OP auto operator() (
                output_stream_device auto& device,
                std::span<const std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t total = buffer.size();
                if (total == 0) return 0;
                std::size_t remain = total;
                do {
                    remain -= device.write_some(buffer.subspan(total - remain, remain));
                }
                while (remain > 0);
                return total;
            }

            COIO_STATIC_CALL_OP auto operator() (
                output_stream_device auto& device,
                dynamic_buffer auto& dyn_buffer
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t bytes_transferred = write_t{}(device, dyn_buffer.data());
                dyn_buffer.consume(bytes_transferred);
                return bytes_transferred;
            }
        };

        struct read_at_t {
            COIO_STATIC_CALL_OP auto operator() (
                input_random_access_device auto& device,
                std::size_t offset,
                std::span<std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t total = buffer.size();
                if (total == 0) return 0;
                std::size_t remain = total;
                do {
                    const auto bytes_transferred = device.read_some_at(offset, buffer.subspan(total - remain, remain));
                    offset += bytes_transferred;
                    remain -= bytes_transferred;
                }
                while (remain > 0);
                return total;
            }

            COIO_STATIC_CALL_OP auto operator() (
                input_random_access_device auto& device,
                std::size_t offset,
                dynamic_buffer auto& dyn_buffer,
                std::size_t total
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                std::size_t bytes_transferred = read_at_t{}(device, offset, dyn_buffer.prepare(total));
                COIO_ASSERT(bytes_transferred == total);
                dyn_buffer.commit(bytes_transferred);
                return total;
            }
        };

        struct write_at_t {
            COIO_STATIC_CALL_OP auto operator() (
                output_random_access_device auto& device,
                std::size_t offset,
                std::span<std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t total = buffer.size();
                if (total == 0) return 0;
                std::size_t remain = total;
                do {
                    const auto bytes_transferred = device.write_some_at(offset, buffer.subspan(total - remain, remain));
                    offset += bytes_transferred;
                    remain -= bytes_transferred;
                }
                while (remain > 0);
                return total;
            }

            COIO_STATIC_CALL_OP auto operator() (
                output_random_access_device auto& device,
                std::size_t offset,
                dynamic_buffer auto& dyn_buffer,
                std::size_t total
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                const std::size_t bytes_transferred = write_at_t{}(device, offset, dyn_buffer.data());
                dyn_buffer.consume(bytes_transferred);
                return bytes_transferred;
            }
        };

        struct read_until_t {
            // Read until char delimiter
            COIO_STATIC_CALL_OP auto operator() (
                input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                char delim
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                std::size_t search_pos = 0;
                for (;;) {
                    auto data = buffer.data();
                    auto begin = reinterpret_cast<const char*>(data.data());
                    auto end = begin + data.size();

                    for (auto it = begin + search_pos; it != end; ++it) {
                        if (*it == delim) {
                            return static_cast<std::size_t>(it - begin + 1);
                        }
                    }

                    search_pos = data.size();

                    std::size_t bytes_to_read = std::max<std::size_t>(512, buffer.capacity() - buffer.size());
                    if (bytes_to_read == 0) bytes_to_read = 512;

                    auto prep = buffer.prepare(bytes_to_read);
                    std::size_t n = device.read_some(prep);
                    if (n == 0) return 0;
                    buffer.commit(n);
                }
            }

            // Read until string delimiter
            COIO_STATIC_CALL_OP auto operator() (
                input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                std::string_view delim
            ) COIO_STATIC_CALL_OP_CONST -> std::size_t {
                if (delim.empty()) return 0;
                if (delim.size() == 1) {
                    return read_until_t{}(device, buffer, delim[0]);
                }

                std::size_t search_pos = 0;
                for (;;) {
                    auto data = buffer.data();
                    auto begin = reinterpret_cast<const char*>(data.data());
                    auto size = data.size();

                    // Back up for partial matches at boundary
                    std::size_t start = search_pos > delim.size() - 1 ? search_pos - delim.size() + 1 : 0;

                    if (size >= delim.size()) {
                        for (std::size_t i = start; i <= size - delim.size(); ++i) {
                            if (std::memcmp(begin + i, delim.data(), delim.size()) == 0) {
                                return i + delim.size();
                            }
                        }
                    }

                    search_pos = size;

                    std::size_t bytes_to_read = std::max<std::size_t>(512, buffer.capacity() - buffer.size());
                    if (bytes_to_read == 0) bytes_to_read = 512;

                    auto prep = buffer.prepare(bytes_to_read);
                    std::size_t n = device.read_some(prep);
                    if (n == 0) return 0;
                    buffer.commit(n);
                }
            }
        };

        template<typename Rcvr>
        struct transfer_bytes_state_base {
            using operation_state_concept = execution::operation_state_t;
            using restart_fn_t = void(*)(transfer_bytes_state_base*) noexcept;

            struct receiver {
                using receiver_concept = execution::receiver_t;

                struct env {
                    template<typename Prop, typename... Args>
                        requires std::default_initializable<Prop> and (forwarding_query(Prop{}))
                            and std::invocable<Prop, execution::env_of_t<Rcvr>, Args...>
                    COIO_ALWAYS_INLINE auto query(const Prop& prop, Args&&... args) const noexcept {
                        return prop(inner, std::forward<Args>(args)...);
                    }

                    execution::env_of_t<Rcvr> inner;
                };

                COIO_ALWAYS_INLINE auto get_env() const noexcept {
                    return env{execution::get_env(state_->rcvr)};
                }

                COIO_ALWAYS_INLINE auto set_value(std::size_t bytes_transferred, bool again) && noexcept -> void {
                    state_->accumulated += bytes_transferred;
                    if (again) {
                        state_->restart(state_);
                    }
                    else {
                        execution::set_value(std::move(state_->rcvr), std::error_code{}, state_->accumulated);
                    }
                }

                COIO_ALWAYS_INLINE auto set_error(std::error_code ec) && noexcept -> void {
                    execution::set_value(std::move(state_->rcvr), ec, state_->accumulated);
                }

                COIO_ALWAYS_INLINE auto set_stopped() && noexcept -> void {
                    std::move(*this).set_error(std::make_error_code(std::errc::operation_canceled));
                }

                transfer_bytes_state_base* state_;
            };

            const restart_fn_t restart;
            Rcvr rcvr;
            std::size_t accumulated = 0;
        };

        template<typename Factory>
        struct transfer_bytes_sender {
            static_assert(std::invocable<Factory&> and execution::sender<std::invoke_result_t<Factory&>>);

            using sender_concept = execution::sender_t;

            using completion_signatures = execution::completion_signatures<
                execution::set_value_t(std::error_code, std::size_t)
            >;

            template<typename Rcvr>
            struct state : transfer_bytes_state_base<Rcvr> {
                using base = transfer_bytes_state_base<Rcvr>;
                using receiver = typename base::receiver;

                state(Rcvr rcvr, Factory factory) noexcept : base(&restart, std::move(rcvr)), factory(std::move(factory)) {
                    produce();
                }

                state(const state&) = delete;

                auto operator= (const state&) -> state& = delete;

                COIO_ALWAYS_INLINE auto start() & noexcept -> void {
                    execution::start(*inner_state);
                }

                COIO_ALWAYS_INLINE auto produce() & noexcept -> void {
                    inner_state.emplace(elide{execution::connect, std::invoke(factory), receiver{this}});
                }

                COIO_ALWAYS_INLINE static auto restart(base* self) noexcept -> void {
                    auto this_ = static_cast<state*>(self);
                    this_->produce();
                    this_->start();
                }

                Factory factory;
                std::optional<execution::connect_result_t<std::invoke_result_t<Factory&>, receiver>> inner_state;
            };

            template<execution::receiver_of<completion_signatures> Rcvr>
            COIO_ALWAYS_INLINE auto connect(Rcvr rcvr) && noexcept {
                return state{std::move(rcvr), std::move(factory)};
            }

            COIO_ALWAYS_INLINE static auto get_env() noexcept {
                return execution::env{execution::prop{
                    execution::get_await_completion_adaptor,
                    execution::let_value([](std::error_code ec, std::size_t bytes_transferred) noexcept {
                        async_result<std::size_t, std::error_code> result;
                        if (ec) {
                            if (ec == std::errc::operation_canceled) result.set_stopped();
                            else result.set_error(ec);
                        }
                        else result.set_value(bytes_transferred);
                        return result;
                    }
                )}};
            }

            Factory factory;
        };

        template<typename Source, typename Continuation, typename... Datas> requires std::invocable<Source&, Datas&...> and std::invocable<Continuation&, std::size_t, Datas&...>
        struct io_sender_factory {
            io_sender_factory(Source src, Continuation cont, Datas... datas) noexcept : src(std::move(src)), cont(std::move(cont)), datas(std::move(datas)...) {}

            io_sender_factory(const io_sender_factory&) = delete;

            io_sender_factory(io_sender_factory&&) noexcept = default;

            auto operator= (const io_sender_factory&) noexcept -> io_sender_factory& = delete;

            auto operator= (io_sender_factory&&) noexcept -> io_sender_factory& = default;

            COIO_ALWAYS_INLINE auto operator()() & noexcept {
                return let_value(std::apply(src, datas), [this](std::size_t bytes_transferred) noexcept {
                    return just(bytes_transferred, [&]<std::size_t... I>(std::index_sequence<I...>) noexcept -> bool {
                        return std::invoke(cont, bytes_transferred, std::get<I>(datas)...);
                    }(std::index_sequence_for<Datas...>{}));
                });
            }

            COIO_NO_UNIQUE_ADDRESS Source src;
            COIO_NO_UNIQUE_ADDRESS Continuation cont;
            std::tuple<Datas...> datas;
        };

        struct async_read_t {
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_input_stream_device auto& device,
                std::span<std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST {
               return transfer_bytes_sender{io_sender_factory{
                   [](auto* device, std::span<std::byte> remaining) noexcept {
                       return device->async_read_some(remaining);
                   },
                   [](std::size_t bytes_transferred, auto, std::span<std::byte>& buffer) noexcept {
                       buffer = buffer.subspan(bytes_transferred);
                       return not buffer.empty();
                   },
                   std::addressof(device),
                   buffer
               }};
            }

            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_input_stream_device auto& device,
                dynamic_buffer auto& dyn_buffer,
                std::size_t total
            ) COIO_STATIC_CALL_OP_CONST {
                return let_value(
                    async_read_t{}(device, dyn_buffer.prepare(total)),
                    [total, &dyn_buffer](std::error_code ec, std::size_t bytes_transferred) noexcept {
                        COIO_ASSERT(ec or bytes_transferred == total);
                        dyn_buffer.commit(bytes_transferred);
                        return just(ec, bytes_transferred);
                    }
                );
            }
        };

        struct async_write_t {
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_output_stream_device auto& device,
                std::span<const std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST {
                return transfer_bytes_sender{io_sender_factory{
                    [](auto* device, std::span<const std::byte> remaining) noexcept {
                        return device->async_write_some(remaining);
                    },
                    [](std::size_t bytes_transferred, auto, std::span<const std::byte>& buffer) noexcept {
                        buffer = buffer.subspan(bytes_transferred);
                        return not buffer.empty();
                    },
                    std::addressof(device),
                    buffer
                }};
            }

            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_output_stream_device auto& device,
                dynamic_buffer auto& dyn_buffer
            ) COIO_STATIC_CALL_OP_CONST {
                return let_value(
                    async_write_t{}(device, dyn_buffer.data()),
                    [&dyn_buffer](std::error_code ec, std::size_t bytes_transferred) noexcept {
                        dyn_buffer.consume(bytes_transferred);
                        return just(ec, bytes_transferred);
                    }
                );
            }
        };


        struct async_read_at_t {
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_input_random_access_device auto& device,
                std::size_t offset,
                std::span<std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST {
               return transfer_bytes_sender{io_sender_factory{
                   [](auto* device, std::size_t offset, std::span<std::byte> remaining) noexcept {
                       return device->async_read_some_at(offset, remaining);
                   },
                   [](std::size_t bytes_transferred, auto, std::size_t& offset, std::span<std::byte>& buffer) noexcept {
                       offset += bytes_transferred;
                       buffer = buffer.subspan(bytes_transferred);
                       return not buffer.empty();
                   },
                   std::addressof(device),
                   offset,
                   buffer
               }};
            }

            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_input_random_access_device auto& device,
                std::size_t offset,
                dynamic_buffer auto& dyn_buffer,
                std::size_t total
            ) COIO_STATIC_CALL_OP_CONST {
                return let_value(
                    async_read_at_t{}(device, offset, dyn_buffer.prepare(total)),
                    [total, &dyn_buffer](std::error_code ec, std::size_t bytes_transferred) noexcept {
                        COIO_ASSERT(ec or bytes_transferred == total);
                        dyn_buffer.commit(bytes_transferred);
                        return just(ec, bytes_transferred);
                    }
                );
            }
        };

        struct async_write_at_t {
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_output_stream_device auto& device,
                std::size_t offset,
                std::span<const std::byte> buffer
            ) COIO_STATIC_CALL_OP_CONST {
                return transfer_bytes_sender{io_sender_factory{
                    [](auto* device, std::size_t offset, std::span<const std::byte> remaining) noexcept {
                        return device->async_write_some_at(offset, remaining);
                    },
                    [](std::size_t bytes_transferred, auto, std::size_t& offset, std::span<const std::byte>& buffer) noexcept {
                        offset += bytes_transferred;
                        buffer = buffer.subspan(bytes_transferred);
                        return not buffer.empty();
                    },
                    std::addressof(device),
                    offset,
                    buffer
                }};
            }

            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_output_random_access_device auto& device,
                std::size_t offset,
                dynamic_buffer auto& dyn_buffer
            ) COIO_STATIC_CALL_OP_CONST {
                return let_value(
                    async_write_at_t{}(device, offset, dyn_buffer.data()),
                    [&dyn_buffer](std::error_code ec, std::size_t bytes_transferred) noexcept {
                        dyn_buffer.consume(bytes_transferred);
                        return just(ec, bytes_transferred);
                    }
                );
            }
        };

        struct async_read_until_t {
            // Read until char delimiter
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                char delim
            ) COIO_STATIC_CALL_OP_CONST {
                return async_read_until_t{}(
                    std::allocator_arg,
                    std::allocator<void>{},
                    device,
                    buffer,
                    delim
                );
            }

            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                std::allocator_arg_t,
                const auto&,
                async_input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                char delim
            ) COIO_STATIC_CALL_OP_CONST -> task<std::size_t> {
                std::size_t search_pos = 0;
                for (;;) {
                    auto data = buffer.data();
                    auto begin = reinterpret_cast<const char*>(data.data());
                    auto end = begin + data.size();

                    for (auto it = begin + search_pos; it != end; ++it) {
                        if (*it == delim) {
                            co_return static_cast<std::size_t>(it - begin + 1);
                        }
                    }

                    search_pos = data.size();

                    std::size_t bytes_to_read = std::max<std::size_t>(512, buffer.capacity() - buffer.size());
                    if (bytes_to_read == 0) bytes_to_read = 512;

                    auto prep = buffer.prepare(bytes_to_read);
                    std::size_t n = co_await device.async_read_some(prep);
                    if (n == 0) co_return 0;
                    buffer.commit(n);
                }
            }

            
            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                async_input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                std::string_view delim
            ) COIO_STATIC_CALL_OP_CONST {
                return async_read_until_t{}(
                    std::allocator_arg,
                    std::allocator<void>{},
                    device,
                    buffer,
                    delim
                );
            }

            [[nodiscard]]
            COIO_STATIC_CALL_OP auto operator() (
                std::allocator_arg_t,
                const auto& alloc,
                async_input_stream_device auto& device,
                dynamic_buffer auto& buffer,
                std::string_view delim
            ) COIO_STATIC_CALL_OP_CONST -> task<std::size_t> {
                if (delim.empty()) co_return 0;
                if (delim.size() == 1) {
                    co_return co_await async_read_until_t{}(
                        std::allocator_arg,
                        alloc,
                        device,
                        buffer,
                        delim[0]
                    );
                }
                std::size_t search_pos = 0;
                for (;;) {
                    auto data = buffer.data();
                    auto begin = reinterpret_cast<const char*>(data.data());
                    auto size = data.size();

                    std::size_t start = search_pos > delim.size() - 1 ? search_pos - delim.size() + 1 : 0;

                    if (size >= delim.size()) {
                        for (std::size_t i = start; i <= size - delim.size(); ++i) {
                            if (std::memcmp(begin + i, delim.data(), delim.size()) == 0) {
                                co_return i + delim.size();
                            }
                        }
                    }

                    search_pos = size;

                    std::size_t bytes_to_read = std::max<std::size_t>(512, buffer.capacity() - buffer.size());
                    if (bytes_to_read == 0) bytes_to_read = 512;

                    auto prep = buffer.prepare(bytes_to_read);
                    std::size_t n = co_await device.async_read_some(prep);
                    if (n == 0) co_return 0;
                    buffer.commit(n);
                }
            }
        };

        struct as_bytes_t {
            template<typename... Args> requires requires { std::span(std::declval<Args>()...); }
            [[nodiscard]]
            COIO_ALWAYS_INLINE
            COIO_STATIC_CALL_OP
            auto operator() (Args&&... args) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(std::span(std::declval<Args>()...)))
            -> std::span<const std::byte> {
                return std::as_bytes(std::span(std::forward<Args>(args)...));
            }
        };

        struct as_writable_bytes_t {
            template<typename... Args> requires requires { std::span(std::declval<Args>()...); }
            [[nodiscard]]
            COIO_ALWAYS_INLINE
            COIO_STATIC_CALL_OP
            auto operator() (Args&&... args) COIO_STATIC_CALL_OP_CONST noexcept(noexcept(std::span(std::declval<Args>()...)))
            -> std::span<std::byte> {
                return std::as_writable_bytes(std::span(std::forward<Args>(args)...));
            }
        };
    }

    inline constexpr detail::read_t                 read{};
    inline constexpr detail::write_t                write{};
    inline constexpr detail::async_read_t           async_read{};
    inline constexpr detail::async_write_t          async_write{};
    inline constexpr detail::read_at_t              read_at{};
    inline constexpr detail::write_at_t             write_at{};
    inline constexpr detail::async_read_at_t        async_read_at{};
    inline constexpr detail::async_write_at_t       async_write_at{};
    inline constexpr detail::read_until_t           read_until{};
    inline constexpr detail::async_read_until_t     async_read_until{};
    inline constexpr detail::as_bytes_t             as_bytes{};
    inline constexpr detail::as_writable_bytes_t    as_writable_bytes{};
}