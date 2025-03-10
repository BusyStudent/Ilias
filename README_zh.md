# Ilias

## 一个纯头的cpp的异步协程Io库 基于cpp20的无栈协程

### 简介

一个旨在最小的依赖的轻量的异步协程库, 基于cpp20的无栈协程实现

- 核心没有其他依赖
- 有内建的取消操作的基本设施
- 结构化并发
- 支持网络 (Tcp, Udp, UnixSocket, Async-GetAddrinfo)
- 支持文件读写和管道
- 支持SSL (在Windows上使用Schannel, 在其他平台上使用OpenSSL)
- 内建小型的HTTP1.1 客户端支持和 WebSocket 客户端支持
- 跨平台 (Windows, Linux)
- 简单的单线程调度器, 易于集成进其他的框架如Qt

### [English](README.md) | [中文](README_zh.md)

### CI 状态

| CI Name | Status |
| --------- | ------- |
| Windows  | [![CI for windows by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml) |
| Linux    | [![CI for linux by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml) |
| Coverage | [![codecov](https://codecov.io/gh/BusyStudent/Ilias/graph/badge.svg?token=W9MQGIPX6F)](https://codecov.io/gh/BusyStudent/Ilias)|

## 快速使用

### 加入你的项目

## 使用xmake的项目

```lua
add_repositories("btk-repo https://github.com/Btk-Project/xmake-repo.git")
add_requires("ilias")
```

## 或者git submodule

```bash
git submodule add https://github.com/BusyStudent/Ilias.git
```

## 如果你想更简单一点

直接把include 里面文件全部复制到你的项目里面就可以了

### 最基本的环境

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt; // 首先 先构建一个上下文 用于提交任务 这是thread_local的 每个线程只有一个
    // 目前可用的Io上下文有 IocpContext EpollContext UringContext QIoContext
    // 没有Io的简单执行器有 MiniExecutor
    // PlatformContext 是个typedef 会根据当前编译时候时候的平台来选择
    auto task = []() -> ilias::Task<int> { // 这是协程 返回值需要是 Task<T>
        co_return 1;
    };
    auto result = task().wait(); // 创建任务 并堵塞等待完成
    // Task<T> 代表返回值为T 所以下面可以用T了
    assert(result == 1);
    return 0;
}
```

如果你想偷个懒 有个 ilias_main 宏 他等价于上面的写法

```cpp

#include <ilias/platform.hpp>
#include <ilias/task.hpp>

void ilias_main() {
    co_return;
}
// 或者
int ilias_main() {
    co_return 0;
}
// () 里面支持 () 和 (int argc, char** argv) 两种格式
// 返回值支持 void 和 int

```

### Socket

简单的发个消息

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpClient;
using ilias::IPEndpoint;

void ilias_main() { // 偷个懒 :)
    auto endpoint = IPEndpoint::fromString("127.0.0.1:8080").value();
    auto client = (co_await TcpClient::make(endpoint.family)).value();
    if (auto res = co_await client.connect(endpoint); !res) {
        co_return;
    }
    // ilias::makeBuffer 会把任何可以到 std::span<T>的类给 转化成 std::span<const std::byte> 或者 std::span<std::byte>
    // read 和 write 的参数分别为 std::span<const std::byte> 和 std::span<std::byte>
    // read 和 write 会返回一个 IoTask<size_t>
    // 而 IoTask<T, E = Error> 是一个 Task<Result<T, E> > 的别名 代表着可能有错误 具体的内容看错误处理部分
    std::string_view sv = "HELLO WORLD";
    if (auto res = co_await client.write(ilias::makeBuffer(sv)); !res) {
        co_return;
    }

    // 还有种方式构造 TcpClient
    auto ctxt = co_await ilias::currentIoContext();
    TcpClient client2(ctxt, AF_INET);
}

等待接受连接

``` cpp
#include <ilias/sync/scope.hpp>
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpListener;
using ilias::IPEndpoint;
using ilias::TaskScope;

void ilias_main() { // 偷个懒 这里面都不处理错误 只是个演示 所以直接用 value() 获取值 :)
    auto endpoint = IPEndpoint::fromString("127.0.0.1:8080").value();
    auto scope = co_await TaskScope::make(); // TaskScope 是一个协程作用域，可以保证在协程退出的时候等待所有子协程完成
    auto listener = (co_await TcpListener::make(endpoint.family)).value();
    while (true) {
        auto [client, _] = (co_await listener.accept()).value();
        auto handle = scope.spawn(handleClient, std::move(client)); // 在scope里面创建一个子任务 当然也可以不用scope 直接用 ilias::spawn 但是这个安全性会差一些
        // 这个handle可以用于检查是否完成了 或者等待完成 如果丢失了这个handle就和detach一样了
    }
}

```

### Http Client

简单的HTTP请求 支持GET POST HEAD等

```cpp
#include <ilias/platform.hpp>
#include <ilias/http.hpp>
#include <ilias/task.hpp>

void ilias_main() { // 偷个懒 :)
    auto session = co_await ilias::HttpSession::make();
    auto reply = (co_await session.get("https://www.google.com")).value();
    auto content = (co_await reply.text()).value();
    std::cout << "Http to " << reply.url().toString() << " Status code"  << reply.statusCode() << std::endl;
    std::cout << content << std::endl;
}
```

### 错误处理

支持错误码和异常, 类为```Result<T, E = Error>``` 是```Expected<T, E>``` 的别名  
而```Expected<T>``` 会根据当前标准库的实现 选择使用标准库(cpp23) 还是内建的实现

而 ```Error``` 相当于 ```std::error_code``` 你可以自己定义错误码 只需要实现自己的 ```ErrorCategory``` 然后用```ILIAS_DECLARE_ERROR(errc, category)``` 绑定一下就行, 不然就需要手动构造 ```Error``` 对象 如 ```Error(yourCode, categoryReference)```

```cpp

auto fn() -> IoTask<void> {
    auto ret = co_await someTask();
    ret.value(); // 如果没有数值 会抛出BadExpectedAccess<Error>， IoTask<T> 会自动把BadExpectedAccess<Error> 转化为 Error 存在返回值里面
    // 所以你可以手动判断
    if (ret) {
        ret.value();
    }
    else {
        co_return Unexpected(ret.error());
    }
    // 或者使用 try catch
    try {
        ret.value();
    }
    catch (const BadExpectedAccess<Error> &e) {
        auto err = e.error();
    }
    // 或者直接不管 错误类型会向上层传递 (有个限制 错误的类型必须一样 比如 IoTask<void, Error1> 和 IoTask<void, Error2> 不能使用这个)
    ret.value();
    // 或者使用安全的语法糖 他会帮你检查错误并拆开Result 就像Rust里面的?一样 (在支持Statement Expression的时候是0开销的 其他情况使用上面的异常实现)
    auto val = ilias_try(ret);
    auto val2 = ilias_try(co_await someTask());

    // 普通的异常 或者错误错误类型不一样 会逐层向上传递 不会被IoTask<T>内部捕获
    throw 1; //< 异常会在外部 co_await fn(); 或者 fn().wait() 的地方重新抛出
}
```

### 和Qt的交互

```cpp
#include <ilias/platform/qt.hpp>
#include <ilias/http.hpp>
#include <QApplication>

auto main(int argc, char **argv) -> int {
    QApplication app(argc, argv);
    ilias::QIoContext ctxt; // 这个是和 Qt集成的IoContext
    // 下面的代码和上面的一样 开始使用吧
}


### 同步

支持多种同步方式 Channel Mutex whenAny whenAll

- whenAny

```cpp
auto fn() -> Task<void> {
    // 等待两个任务完成 任意一个完成 其他任务就会收到取消通知 并抛弃返回值
    auto [a, b] = co_await whenAny(taskA(), taskB());
    if (a) { //< taskA() 先完成

    }
    if (b) { //< taskB() 先完成

    }
    // Vector 类型
    std::vector<Task<void> > tasks;
    tasks.emplace_back(taskA());
    tasks.emplace_back(taskB());
    for (auto &result : co_await whenAny(tasks)) {
        
    }
}
```

- whenAll

```cpp
auto fn() -> Task<void> {
    // 只有两个都完成了 才会返回
    auto [a, b] = co_await whenAll(taskA(), taskB());
    // use a & b

    // Vector 类型
    std::vector<Task<void> > tasks;
    tasks.emplace_back(taskA());
    tasks.emplace_back(taskB());
    auto res = co_await whenAll(tasks); // 只有一个返回值
}
```

- Channel

```cpp
auto fn() -> Task<void> {
    // 创建一个通道
    auto [sender, receiver] = mpmc::channel<int>(3); // 3 是容量大小 如果send时候将要超容量 会堵塞 如果不填 默认是 size_t::max() 相当于无限大
    co_await sender.send(1);
    auto val = co_await receiver.recv();
}
```

### 依赖

- zlib (可选)
- openssl (可选)
- liburing (可选, 被 UringContext使用)

### 后端

| Backend | Progress | Description |
| --------- | ---------- | ------------- |
| epoll     | Completed |         |
| IOCP      | Completed |         |
| Qt        | Completed | Qt 集成 |
| io_uring  | Completed |         |
