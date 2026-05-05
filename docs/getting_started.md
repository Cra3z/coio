# Getting Started

## Requirements

- **C++20** compatible compiler:
    - GCC 12+
    - Clang 16+
    - MSVC 2022 17.4+
- **CMake** 3.26+
- **[liburing](https://github.com/axboe/liburing)** — only if using `uring_context` on Linux

## Build Options

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `COIO_BUILD_EXAMPLES` | `ON`, `OFF` | `OFF` | Build example programs |
| `COIO_SENDERS_BACKEND` | `NVIDIA`, `BEMAN`, `CXX26` | `NVIDIA` | Which `std::execution` implementation to use |

### Sender Backends

coio requires a `std::execution` implementation. Three backends are supported:

| Backend | Dependency | Notes |
|---------|-----------|-------|
| `NVIDIA` | [NVIDIA/stdexec](https://github.com/NVIDIA/stdexec) | Default. Fetched automatically via CPM. |
| `BEMAN` | [bemanproject/execution](https://github.com/bemanproject/execution) | Fetched automatically via CPM. |
| `CXX26` | None (uses `std::execution`) | Requires a C++26 standard library with `<execution>`. |

## Building the Library

### Basic Build

```shell
cmake -S . -B build
cmake --build build
```

### Build with Examples

```shell
cmake -S . -B build -DCOIO_BUILD_EXAMPLES=ON
cmake --build build
```

### Choose a Different Sender Backend

```shell
cmake -S . -B build -DCOIO_SENDERS_BACKEND=BEMAN
cmake --build build
```

### MSVC (Developer Command Prompt)

```shell
cmake -S . -B build
cmake --build build --config Release
```

### Linux with Clang and io_uring

```shell
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

## Installing

```shell
cmake --install build --prefix /path/to/install
```

The install target provides CMake package configuration files, making coio consumable via `find_package`.

## Using coio in Your Project

### Via CMake find_package (after install)

```cmake
find_package(coio REQUIRED)
target_link_libraries(my_target PRIVATE coio::coio)
```

### Via CPM.cmake (recommended)

Add this to your `CMakeLists.txt`:

```cmake
include(cmake/CPM.cmake)  # or fetch CPM

CPMFindPackage(
    NAME coio
    GITHUB_REPOSITORY Cra3z/coio
    GIT_TAG main
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
    OPTIONS
    "COIO_BUILD_EXAMPLES OFF"
)
target_link_libraries(my_target PRIVATE coio::coio)
```

### Via add_subdirectory

```cmake
add_subdirectory(third_party/coio)
target_link_libraries(my_target PRIVATE coio::coio)
```

## Hello, World

```cpp
// hello.cpp
#include <coio/core.h>
#include <coio/execution_context.h>
#include <coio/utils/timer.h>
#include <iostream>

auto hello() -> coio::task<> {
    std::cout << "Hello, ";
    co_await coio::timer{co_await coio::execution::read_env(
        coio::execution::get_scheduler)}.async_wait(std::chrono::seconds{1});
    std::cout << "World!\n";
}

int main() {
    coio::time_loop context;
    coio::this_thread::sync_wait(
        coio::on(context.get_scheduler(), hello())
    );
}
```

Compile and run:

```shell
# Assuming coio is in third_party/coio
cmake -S . -B build
cmake --build build
./build/hello
```

## Next Steps

- See the [Reference](reference/index.md) for complete API documentation
- Read the [Guides](guides/index.md) for task-based tutorials
- Browse the [Examples](https://github.com/Cra3z/coio/tree/main/examples) directory for full working programs
