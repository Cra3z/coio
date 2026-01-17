en | [zh-cn](README.zh-CN.md)  
# coio (coroutine input/ouput)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)

coio provides some basic library facilities for *C++20 coroutines*.  

<details>
<summary> what's C++20 coroutine? </summary>

* [https://en.cppreference.com/w/cpp/language/coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
* [https://lewissbaker.github.io/2017/09/25/coroutine-theory](https://lewissbaker.github.io/2017/09/25/coroutine-theory)
* [https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/](https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/coroutines-c++20/)
</details>

> [!NOTE]
> Some network and async-io facilities are currently only implemented using epoll and io_uring on linux.

## Build and Install

### Requirements
- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- CMake 3.26+

### Build Options
- `COIO_BUILD_EXAMPLES` (ON/OFF, default OFF) - Build example programs
- `COIO_ENABLE_SENDERS` (ON/OFF, default OFF) - Enable std::execution (P2300) support
- `COIO_SENDERS_BACKEND` (NVIDIA/BEMAN/CXX26, default NVIDIA) - Which std::execution implementation to use:
  - `NVIDIA` - NVIDIA/stdexec implementation
  - `BEMAN` - bemanproject/execution implementation  
  - `CXX26` - Standard library implementation (C++26+)

### Basic Build
```shell
cmake -S . -B <build directory>
cmake --build <build directory>
```

### Build with Examples
```shell
cmake -S . -B <build directory> -DCOIO_BUILD_EXAMPLES=ON
cmake --build <build directory>
```

### Build with std::execution Support
```shell
cmake -S . -B <build directory> -DCOIO_ENABLE_SENDERS=ON -DCOIO_SENDERS_BACKEND=NVIDIA
cmake --build <build directory>
```

### Install
```shell
cmake --install <build directory> --prefix <install directory>
```

### CMake Usage
After installation, use in your CMakeLists.txt:
```cmake
find_package(coio REQUIRED)
target_link_libraries(your_target PRIVATE coio::coio)
```
### Examples
See [examples](examples)

### Documents
See [docs](docs)