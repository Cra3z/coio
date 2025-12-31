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
> 部分网络和异步IO设施目前仅在Linux上使用epoll实现。

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

## 公开 API

### 协程类型

#### `task<T, Alloc>` (`#include <coio/task.h>`)
惰性启动、仅可移动的协程类型，表示产生类型 `T` 值的异步计算。
- 模板参数: `T` (结果类型, 默认 `void`), `Alloc` (分配器类型, 默认 `void`)
- 可等待: 是 (仅移动)

#### `shared_task<T, Alloc>` (`#include <coio/task.h>`)
惰性启动、引用计数的协程类型，可被多个协程多次等待。
- 模板参数: `T` (结果类型, 默认 `void`), `Alloc` (分配器类型, 默认 `void`)
- 可等待: 是 (可拷贝)

#### `generator<Ref, Val, Alloc>` (`#include <coio/generator.h>`)
同步生成器，使用 `co_yield` 惰性产生值序列。
- 模板参数: `Ref` (引用类型), `Val` (值类型, 默认推导), `Alloc` (分配器类型, 默认 `void`)
- 模型: `std::ranges::view_interface`
- 通过 `elements_of` 支持递归生成

### 执行上下文与调度器

#### `run_loop` (`#include <coio/execution_context.h>`)
基本的执行上下文，用于运行协程。
- `get_scheduler()` - 获取关联的调度器
- `run()` / `run_one()` - 运行就绪操作 (阻塞)
- `poll()` / `poll_one()` - 运行就绪操作 (非阻塞)
- `request_stop()` - 请求停止运行循环

#### `epoll_context` (`#include <coio/asyncio/epoll_context.h>`) [仅Linux]
类似于`run_loop`, 但基于epoll支持异步I/O操作.

#### `work_guard<ExecutionContext>` (`#include <coio/execution_context.h>`)
RAII 守卫，通过维护未完成工作计数来保持执行上下文运行。

#### `inline_scheduler` (`#include <coio/schedulers.h>`)
立即内联执行工作的调度器，不挂起。

#### 调度器 CPO (`#include <coio/schedulers.h>`)
- `schedule(scheduler)` - 获取在调度器上完成的 sender/awaitable
- `starts_on(scheduler, awaitable)` - 在特定调度器上启动 awaitable
- `continues_on(awaitable, scheduler)` - awaitable 完成后在特定调度器上继续执行
- `on(scheduler, awaitable)` - 完全在特定调度器上执行 awaitable

### 协程工具

#### CPO (`#include <coio/core.h>`)
- `when_all(awaitables...)` - 并发等待多个 awaitable，返回结果元组
- `sync_wait(awaitable)` - 同步等待 awaitable 完成
- `then(awaitable, fn)` - 将延续函数链接到 awaitable
- `just(value)` - 创建立即产生值的 awaitable
- `just_error(error)` - 创建立即产生错误的 awaitable
- `just_stopped()` - 创建立即停止的 awaitable
- `split(awaitable)` - 将 awaitable 转换为 shared_task

#### `async_scope` (`#include <coio/core.h>`)
用于生成和管理即发即忘异步工作的作用域。
- `spawn(awaitable)` - 在后台生成运行的 awaitable
- `join()` - 等待所有生成的工作完成

### 定时器

#### `timer<Scheduler>` (`#include <coio/timer.h>`)
用于调度延迟操作的定时器工具。
- `wait(duration)` - 同步等待
- `wait_until(time_point)` - 同步等待到时间点
- `async_wait(duration)` - 异步等待
- `async_wait_until(time_point)` - 异步等待到时间点

### 同步原语

#### `async_mutex` (`#include <coio/sync_primitives.h>`)
协程异步互斥锁。
- `lock()` - 异步获取锁 (awaitable)
- `try_lock()` - 尝试同步获取锁
- `unlock()` - 释放锁
- `lock_guard()` - 获取并返回 RAII 守卫 (awaitable)

#### `async_unique_lock<AsyncMutex>` (`#include <coio/sync_primitives.h>`)
异步互斥锁的 RAII 锁包装器。
- `lock()` - 异步获取 (awaitable)
- `try_lock()` - 尝试同步获取
- `unlock()` - 释放锁
- `owns_lock()` - 检查是否持有锁

#### `async_semaphore<LeastMaxValue>` (`#include <coio/sync_primitives.h>`)
异步计数信号量。
- `acquire()` - 异步获取 (awaitable)
- `try_acquire()` - 尝试同步获取
- `release()` - 释放 (awaitable)
- `count()` - 获取当前计数

#### `async_binary_semaphore` (`#include <coio/sync_primitives.h>`)
`async_semaphore<1>` 的别名。

#### `async_latch` (`#include <coio/sync_primitives.h>`)
一次性同步的异步闩锁。
- `count_down(n)` - 递减计数器
- `wait()` - 等待计数器归零 (awaitable)
- `arrive_and_wait(n)` - 递减并等待 (awaitable)
- `try_wait()` - 检查计数器是否归零

### 网络

#### 地址类型 (`#include <coio/net/basic.h>`)
- `ipv4_address` - IPv4 地址表示
- `ipv6_address` - IPv6 地址表示
- `ip_address` - IPv4/IPv6 地址的变体
- `endpoint` - IP 地址 + 端口组合

#### 协议类型
- `tcp` (`#include <coio/net/tcp.h>`) - TCP 协议描述符
  - `tcp::v4()` / `tcp::v6()` - 获取 IPv4/IPv6 TCP 协议
  - `tcp::acceptor<IoScheduler>` - TCP 接受器套接字类型别名
  - `tcp::socket<IoScheduler>` - TCP 流套接字类型别名
  - `tcp::resolver` - TCP 解析器类型别名

- `udp` (`#include <coio/net/udp.h>`) - UDP 协议描述符
  - `udp::v4()` / `udp::v6()` - 获取 IPv4/IPv6 UDP 协议
  - `udp::socket<IoScheduler>` - UDP 数据报套接字类型别名
  - `udp::resolver` - UDP 解析器类型别名

#### 套接字类 (`#include <coio/net/socket.h>`)

##### `basic_socket<Protocol, IoScheduler>`
基本套接字类，包含通用套接字操作。
- `open(protocol)` / `close()` - 打开/关闭套接字
- `bind(endpoint)` - 绑定到本地端点
- `connect(endpoint)` - 连接到远程端点 (同步)
- `async_connect(endpoint)` - 异步连接 (awaitable)
- `local_endpoint()` / `remote_endpoint()` - 获取端点
- `set_option(opt)` / `get_option(opt)` - 套接字选项
- `cancel()` - 取消挂起的异步操作
- `shutdown(how)` - 关闭发送/接收

##### `basic_socket_acceptor<Protocol, IoScheduler>`
用于接受传入连接的服务器套接字。
- `listen(backlog)` - 开始监听
- `accept()` / `accept(scheduler)` - 接受连接 (同步)
- `async_accept()` / `async_accept(scheduler)` - 异步接受 (awaitable)

##### `basic_stream_socket<Protocol, IoScheduler>`
TCP 连接的流套接字。
- `read_some(buffer)` / `write_some(buffer)` - 同步 I/O
- `receive(buffer)` / `send(buffer)` - 同步 I/O (别名)
- `async_read_some(buffer)` / `async_write_some(buffer)` - 异步 I/O (awaitable)
- `async_receive(buffer)` / `async_send(buffer)` - 异步 I/O (别名, awaitable)

##### `basic_datagram_socket<Protocol, IoScheduler>`
UDP 数据报套接字。
- `receive(buffer)` / `send(buffer)` - 同步 I/O
- `receive_from(buffer, endpoint)` / `send_to(buffer, endpoint)` - 带端点的同步 I/O
- `async_receive(buffer)` / `async_send(buffer)` - 异步 I/O (awaitable)
- `async_receive_from(buffer, endpoint)` / `async_send_to(buffer, endpoint)` - 带端点的异步 I/O (awaitable)

#### 解析器 (`#include <coio/net/resolver.h>`)
##### `resolver<Protocol>`
用于主机名/服务解析的 DNS 解析器。
- `resolve(query)` - 将查询解析为端点序列 (返回 `generator<result_t>`)
- `resolve(protocol, query)` - 将查询解析为端点序列 (返回 `generator<result_t>`)
- `async_resolve(query)` - 异步地将查询解析为端点序列 (返回 `generator<result_t>`)
- `async_resolve(protocol, query)` - 异步地将查询解析为端点序列 (返回 `generator<result_t>`)

### 异步 I/O 工具

#### CPO (`#include <coio/asyncio/io.h>`)
- `read(file, buffer)` - 同步读取整个缓冲区
- `write(file, buffer)` - 同步写入整个缓冲区
- `async_read(file, buffer)` - 异步读取整个缓冲区 (awaitable)
- `async_write(file, buffer)` - 异步写入整个缓冲区 (awaitable)
- `as_bytes(...)` - 转换为 `std::span<const std::byte>`
- `as_writable_bytes(...)` - 转换为 `std::span<std::byte>`

### 容器

#### `inplace_vector<T, N>` (`#include <coio/utils/inplace_vector.h>`)
具有内联存储的固定容量向量（无堆分配）。
- 容量 `N` 在编译时固定
- 模型: `std::ranges::contiguous_range`, `std::ranges::sized_range`
- 完整的 STL vector 类接口

#### `basic_fixed_string<CharType, N>` (`#include <coio/utils/fixed_string.h>`)
编译时容量固定的字符串。
- `fixed_string<N>` - `basic_fixed_string<char, N>` 的别名
- 可用作非类型模板参数

#### `conqueue<T, Alloc, Container>` (`#include <coio/utils/conqueue.h>`)
线程安全的异步阻塞队列（多生产者，多消费者）。
- `push(value)` / `emplace(args...)` - 添加元素 (awaitable)
- `pop()` - 移除并返回元素 (awaitable)
- `try_pop()` - 尝试非阻塞弹出 (awaitable, 返回 optional)
- `pop_all()` - 移除所有元素 (awaitable)

#### `ring_buffer<T, Container>` (`#include <coio/utils/conqueue.h>`)
固定大小的异步环形缓冲区（单生产者，单消费者）。
- `push(value)` / `emplace(args...)` - 添加元素 (awaitable)
- `pop()` - 移除并返回元素 (awaitable)
- `try_pop()` - 尝试非阻塞弹出 (awaitable, 返回 optional)

#### `inplace_ring_buffer<T, N>` (`#include <coio/utils/conqueue.h>`)
`ring_buffer<T, std::allocator<T>, inplace_vector<T, N>>` 的别名。

### 概念

#### 协程概念 (`#include <coio/concepts.h>`)
- `awaiter<T, Promise>` - 类型可用作协程 awaiter
- `awaitable<T, Promise>` - 类型可与 `co_await` 一起使用
- `awaitable_value<T, Promise>` - 可移动构造的 awaitable

#### 调度器概念 (`#include <coio/schedulers.h>`)
- `scheduler<T>` - 类型建模调度器
- `timed_scheduler<T>` - 具有定时功能的调度器
- `io_scheduler<T>` - 支持 I/O 操作的调度器

#### 互斥锁概念 (`#include <coio/sync_primitives.h>`)
- `basic_async_lockable<T>` - 类型具有异步 `lock()` 和 `unlock()`
- `async_lockable<T>` - basic_async_lockable 且带有 `try_lock()`

#### I/O 概念 (`#include <coio/asyncio/io.h>`)
- `readable_file<T>` - 类型具有 `read_some(buffer)`
- `writable_file<T>` - 类型具有 `write_some(buffer)`
- `async_readable_file<T>` - 类型具有 `async_read_some(buffer)`
- `async_writable_file<T>` - 类型具有 `async_write_some(buffer)`
- `dynamic_buffer<T>` - 类型建模动态缓冲区