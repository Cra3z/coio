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

已实现设施:  
* 协程类型:
  * generator
  * task
  * shared_task
* 网络:
  * endpoint
  * tcp_acceptor
  * tcp_socket
  * udp_socket
  * resolver
* 容器:
  * inplace_vector
  * fixed_string
  * blocking_queue
  * ring_buffer
* 异步io与调度器:
  * io_context
  * round_robin_scheduler
  * noop_scheduler
  * async_write
  * async_read
  * async_mutex
* 其它:
  * when_all
  * sync_wait
  * async_scope
  * steady_timer

需要注意的是network和async-io部分的设施目前仅在linux上使用epoll实现。

## 构建和安装

### 构建
```shell
cmake -S . -B <your build directory>
cmake --build <your build directory>
```
### 构建测试用例
```shell
cmake -S . -B <your build directory> -DCOIO_EXAMPLES=ON
cmake --build <your build directory>
```

### 安装
```shell
cmake --install <your build directory> --prefix <your install directory>
```