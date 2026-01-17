#pragma once
#include <format>
#include <iostream>
#include <sstream>
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
    ::println(std::cout, fmt, std::forward<Args>(args)...);
}

inline auto println(std::ostream& out) -> void {
    out << std::endl;
}

inline auto println() -> void {
    ::println(std::cout);
}

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    auto thread_name = (std::stringstream{} << std::this_thread::get_id()).str();
    std::clog << std::format("[thread-{}] {}\n", thread_name, std::format(fmt, std::forward<Args>(args)...));
}