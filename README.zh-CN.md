简体中文 | [en](README.md)  
# coio (coroutine input/ouput)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)  

coio 提供了一些*C++20 coroutine*的基本库设施。  

<details>
<summary> C++20 coroutine是什么？</summary>

* [https://zh.cppreference.com/w/cpp/language/coroutines](https://zh.cppreference.com/w/cpp/language/coroutines)
* [https://lewissbaker.github.io/2017/09/25/coroutine-theory](https://lewissbaker.github.io/2017/09/25/coroutine-theory)
* [https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
</details>

> [!NOTE]
> 部分网络和异步IO设施目前仅在Linux上使用epoll和io_uring实现。

## 构建和安装

### 系统要求
- C++20 兼容编译器 (GCC 10+, Clang 10+, MSVC 2019+)
- CMake 3.26+

### 构建选项
- `COIO_BUILD_EXAMPLES` (ON/OFF, 默认 OFF) - 构建示例程序
- `COIO_ENABLE_SENDERS` (ON/OFF, 默认 OFF) - 启用 std::execution (P2300) 支持
- `COIO_SENDERS_BACKEND` (NVIDIA/BEMAN/CXX26, 默认 NVIDIA) - 使用哪个 std::execution 实现:
  - `NVIDIA` - NVIDIA/stdexec 实现
  - `BEMAN` - bemanproject/execution 实现
  - `CXX26` - 标准库实现 (C++26+)

### 基础构建
```shell
cmake -S . -B <构建目录>
cmake --build <构建目录>
```

### 构建示例
```shell
cmake -S . -B <构建目录> -DCOIO_BUILD_EXAMPLES=ON
cmake --build <构建目录>
```

### 构建并启用 std::execution 支持
```shell
cmake -S . -B <构建目录> -DCOIO_ENABLE_SENDERS=ON -DCOIO_SENDERS_BACKEND=NVIDIA
cmake --build <构建目录>
```

### 安装
```shell
cmake --install <构建目录> --prefix <安装目录>
```

### CMake 使用
安装后，在你的 CMakeLists.txt 中使用:
```cmake
find_package(coio REQUIRED)
target_link_libraries(your_target PRIVATE coio::coio)
```

### 代码示例
见 [examples](examples)

### 文档
见 [docs](docs)