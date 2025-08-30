# Ilias

## 一个迷你的类似tokio的cpp的异步协程Io库 基于cpp20的无栈协程

### 简介

一个旨在最小的依赖的轻量的异步协程库, 基于cpp20的无栈协程实现

- 核心没有其他依赖
- 支持取消操作
- 结构化并发
- 支持网络 (Tcp, Udp, Async-GetAddrinfo)
- 支持文件读写和管道
- 支持SSL (在Windows上使用Schannel, 在其他平台上使用OpenSSL)
- 跨平台 (Windows, Linux)
- 简单的单线程调度器, 易于集成进其他的框架如Qt

### [English](README_en.md) | [中文](README.md)

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

### 最基本的环境

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt; // 首先 先构建一个上下文 用于提交任务
    ctxt.install(); //注册到当前线程
    // 目前可用的Io上下文有 IocpContext EpollContext UringContext QIoContext
    // 没有Io的简单执行器有 EventLoop
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

using ilias::TcpStream;

void ilias_main() { // 偷个懒 :)
    auto client = (co_await TcpStream::connect("127.0.0.1:8080")).value();
    // ilias::makeBuffer 会把任何可以到 std::span<T>的类给 转化成 std::span<const std::byte>(Buffer) 或者 std::span<std::byte>(MutableBuffer)
    // read 和 write 的参数分别为 Buffer 和 MutableBuffer
    // read 和 write 会返回一个 IoTask<size_t>
    // 而 IoTask<T> 是一个 Task<IoResult<T> > 的别名 代表着可能有错误 具体的内容看错误处理部分
    std::string_view sv = "HELLO WORLD";
    if (auto res = co_await client.write(ilias::makeBuffer(sv)); !res) {
        co_return;
    }
}

等待接受连接

``` cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::IPEndpoint;

void ilias_main() { // 偷个懒 这里面都不处理错误 只是个演示 所以直接用 value() 获取值 :)
    auto listener = (co_await TcpListener::bind("127.0.0.1:8080")).value();
    while (true) {
        auto [client, _] = (co_await listener.accept()).value();
        auto handle = ilias::spawn(handleClient, std::move(client));
        // 这个handle可以用于检查是否完成了 或者等待完成 如果丢失了这个handle就和detach一样了
    }
}

```

### 启动协程

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

void ilias_main() {
    auto handle = ilias::spawn(task()); // 启动一个协程
    auto handle2 = ilias::spawnBlocking(callable); // 启动一个堵塞任务 这个任务会被扔到线程池子上
    // handle 可以用于检查是否完成了 或者等待完成
    co_await std::move(handle);

    // 如果要更好的控制协程的生命周期 可以使用 TaskScope 或者 TaskGroup<T> 来控制 
}
```

### 错误处理

支持错误码和异常, 类为```Result<T, E>``` 是```std::expected<T, E>``` 的别名  
而使用expected实现 会根据当前CPP的版本 选择使用标准库(cpp23) 还是第三方实现 (zeus_expected)

而 ```IoResult<T>``` 相当于 ```Result<T, std::error_code>```

### 和Qt的交互

```cpp
#include <ilias/platform/qt.hpp>
#include <QApplication>

auto main(int argc, char **argv) -> int {
    QApplication app(argc, argv);
    ilias::QIoContext ctxt; // 这个是和 Qt集成的IoContext
    ctxt.install();
    // 下面的代码和上面的一样 开始使用吧
}

```

### 取消

支持取消操作, 取消操作会直接尝试在await点停止执行当前协程 类似于抛出一个不可捕获的异常

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

using namespace std::literals;

void ilias_main() {
    auto task = []() -> ilias::Task<int> {
        co_return 1;
    };
    auto handle = ilias::spawn(task);
    handle.stop();

    // WaitHandle<T> co_await的结果为Option<T>(std::optional的别名 自动把void替换成std::monostate) 如果是nullopt 就代表确实被取消了
    auto res = co_await std::move(handle);
    assert(res == 1); // 因为这个task没有await点 所以取消不会成功

    auto task2 = []() -> ilias::Task<int> {
        co_await ilias::sleep(1000ms);
        co_return 1;
    };
    auto handle2 = ilias::spawn(task2);
    handle2.stop();
    auto res2 = co_await std::move(handle2);
    assert(res2 == std::nullopt); // 因为这个task有await点 而且不大可能在那么短时间内完成 所以取消成功
}
```

### 同步

支持多种同步方式 Channel Mutex whenAny whenAll, TaskGroup

- whenAny

```cpp
auto fn() -> Task<void> {
    // 等待两个任务完成 任意一个完成 其他任务就会收到取消通知 并抛弃返回值
    auto [a, b] = co_await whenAny(taskA(), taskB());
    if (a) { //< taskA() 先完成

    }
    if (b) { //< taskB() 先完成

    }
}
```

- whenAll

```cpp
auto fn() -> Task<void> {
    // 只有两个都完成了 才会返回
    auto [a, b] = co_await whenAll(taskA(), taskB());
    // use a & b
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

- TaskGroup

```cpp
auto fn() -> Task<void> {
    auto group = ilias::TaskGroup<void>(); // 这个 T是返回值, 如果group被析构里面还有 task 那么所有task会收到取消信号并detach
    group.spawn(taskA());
    group.spawn(taskB());

    // 等待所有任务完成  返回 std::vector<T> (void会自动变成std::monostate)
    co_await group.waitAll();

    // 给里面所有的task发取消信号 然后等待所有task完成并抛弃返回值
    co_await group.shutdown(); 

    co_await group.next(); // 等待下一个完成的task 返回 Option<T>
}
```

- TaskScope

``` cpp

auto fn() -> Task<void> {
    auto val = co_await TaskScope::enter([](auto &scope) -> Task<int> {
        scope.spawn(another()); // 可以在scope里启动其他 task 保证只有当所有task 完成 / 取消 时候才会返回
        co_return 42;
    });
    assert(val == 42);
}

```

### 依赖

- openssl (可选)
- liburing (可选, 被 UringContext使用)

### 后端

| Backend | Progress | Description |
| --------- | ---------- | ------------- |
| epoll     | Completed |         |
| IOCP      | Completed |         |
| Qt        | Completed | Qt 集成 |
| io_uring  | Completed |         |
