#pragma once
#include <chrono>
#include <format>
#include <iostream>
#include <syncstream>
#include <thread>
#include <coio/utils/async_result.h>

template<typename... Args>
auto print(std::ostream& out, std::format_string<Args...> fmt, Args&&... args) ->void {
    out << std::format(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
auto print(std::format_string<Args...> fmt, Args&&... args) ->void {
    ::print(std::clog, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
auto println(std::ostream& out, std::format_string<Args...> fmt, Args&&... args) ->void {
    out << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}

template<typename... Args>
auto println(std::format_string<Args...> fmt, Args&&... args) ->void {
    ::println(std::clog, fmt, std::forward<Args>(args)...);
}

inline auto println(std::ostream& out) -> void {
    out << std::endl;
}

inline auto println() -> void {
    ::println(std::clog);
}

template<typename... Args>
auto debug(std::format_string<Args...> fmt, Args&&... args) -> void {
    std::osyncstream{std::clog} <<
        "[" << std::chrono::system_clock::now() << "] " <<
        "[thread-" << std::this_thread::get_id() << "] " <<
        std::format(fmt, std::forward<Args>(args)...) << std::endl;
}

COIO_ALWAYS_INLINE auto dispatch_result(std::error_code ec, std::size_t bytes_transferred) noexcept {
    coio::async_result<coio::execution::set_value_t(std::size_t), coio::execution::set_error_t(std::error_code)> result;
    if (ec) {
        if (ec == std::errc::operation_canceled) result.set_stopped();
        else result.set_error(ec);
    }
    else result.set_value(bytes_transferred);
    return result;
}

inline constexpr auto as_throwing = coio::execution::let_value(dispatch_result);
