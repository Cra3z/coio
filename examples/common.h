#pragma once
#include <format>
#include <iostream>

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

auto println(std::ostream& out) -> void {
    out << std::endl;
}

auto println() -> void {
    ::println(std::cout);
}