#pragma once
#include <chrono>
#include <format>
#include <iostream>
#include <syncstream>
#include <thread>

template<typename... Args>
auto print(std::ostream& out, std::format_string<Args...> fmt, Args&&... args) ->void {
    out << std::format(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
auto print(std::format_string<Args...> fmt, Args&&... args) ->void {
    ::print(std::cout, fmt, std::forward<Args>(args)...);
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