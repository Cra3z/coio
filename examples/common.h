#pragma once
#include <format>
#include <iostream>

template<typename... Args>
auto print(std::format_string<Args...> fmt, Args&&... args) ->void {
    std::cout << std::format(fmt, std::forward<Args>(args)...);
}

template<typename... Args>
auto println(std::format_string<Args...> fmt, Args&&... args) ->void {
    std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}
