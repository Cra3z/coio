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

implemented facilities:  
* coroutine types:
    * generator
    * task
    * shared_task
* network:
    * endpoint
    * tcp_acceptor
    * tcp_socket
    * udp_socket
    * resolver
* containers:
    * inplace_vector
    * fixed_string
    * blocking_queue
    * ring_buffer
* async-io and schedulers:
    * io_context
    * round_robin_scheduler
    * noop_scheduler
    * async_write
    * async_read
    * async_mutex
* others:
    * when_all
    * sync_wait
    * async_scope
    * steady_timer

note that some network and async-io facilities are currently only implemented using epoll on linux.

## build and install

### build
```shell
cmake -S . -B <your build directory>
cmake --build <your build directory>
```
### build examples
```shell
cmake -S . -B <your build directory> -DCOIO_EXAMPLES=ON
cmake --build <your build directory>
```

### install
```shell
cmake --install <your build directory> --prefix <your install directory>
```