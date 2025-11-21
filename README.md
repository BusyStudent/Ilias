# Ilias

> 一个基于 C++20 无栈协程的轻量级异步 IO 库,类似 Tokio 的设计理念

[English](README_en.md) | 中文

## ✨ 特性

- 🚀 零依赖核心库
- ❌ 支持取消操作
- 🔗 结构化并发支持 (通过 TaskScope 和 TaskGroup)
- 🌐 完整的网络支持(TCP / UDP / 异步 DNS 解析)
- 📁 文件、管道 IO
- 🔒 TLS 支持(Windows: Schannel / 其他: OpenSSL)
- 💻 跨平台(Windows / Linux)
- 🎯 单线程调度器,易于集成到 Qt 等框架

## 📖 目录

- [CI 状态](#-ci-状态)
- [快速开始](#快速使用)
  - [加入你的项目](#加入你的项目)
  - [基本环境](#最基本的环境)
- [网络编程](#socket)
- [启动协程](#启动协程)
- [错误处理](#错误处理)
- [Qt 集成](#和-qt-的交互)
- [取消机制](#取消)
- [实用工具](#小工具)
- [同步原语](#同步)
- [依赖和后端](#依赖)
- [系统要求](#系统要求)

## 📊 CI 状态

| CI Name | Status |
| --------- | ------- |
| Windows  | [![CI for windows by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml) |
| Linux    | [![CI for linux by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml) |
| Coverage | [![codecov](https://codecov.io/gh/BusyStudent/Ilias/graph/badge.svg?token=W9MQGIPX6F)](https://codecov.io/gh/BusyStudent/Ilias)|

## 快速使用

### 加入你的项目

#### 使用 xmake 的项目

```lua
add_repositories("btk-repo https://github.com/Btk-Project/xmake-repo.git")
add_requires("ilias")

target("your_app")
    add_packages("ilias")
```

### 最基本的环境

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt; // 构建一个 IO 上下文用于提交任务
    ctxt.install(); // 注册到当前线程
    
    // 目前可用的 IO 上下文有: IocpContext, EpollContext, UringContext, QIoContext
    // 没有 IO 的简单执行器有: EventLoop
    // PlatformContext 是 typedef,会根据编译平台自动选择
    
    auto task = []() -> ilias::Task<int> { // 协程函数,返回值必须是 Task<T>
        co_return 1;
    };
    
    auto result = task().wait(); // 创建任务并阻塞等待完成
    // Task<T> 代表返回值为 T
    assert(result == 1);
    return 0;
}
```

#### 使用 `ilias_main` 宏简化

如果你想简化代码,可以使用 `ilias_main` 宏,它等价于上面的写法

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

void ilias_main() {
    co_return;
}

// 或者返回 int
int ilias_main() {
    co_return 0;
}

// 支持两种参数格式 
// - ilias_main()
// - ilias_main(int argc, char** argv)
// 返回值支持 void 和 int
// 目前不支持 auto -> 的写法 实现的限制
```

### Socket

#### 简单的发送消息

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpStream;

void ilias_main() {
    auto client = (co_await TcpStream::connect("127.0.0.1:8080")).value();
    
    // ilias::makeBuffer 会将任何可转换为 std::span<T> 的类型
    // 转化成 std::span<const std::byte> (Buffer) 或 std::span<std::byte> (MutableBuffer)
    // read 和 write 的参数分别为 MutableBuffer 和 Buffer
    // read 和 write 会返回 IoTask<size_t>
    // IoTask<T> 是 Task<Result<T, std::error_code>> 的别名,代表可能有错误(具体见错误处理部分)
    
    std::string_view sv = "HELLO WORLD";
    if (auto res = co_await client.write(ilias::makeBuffer(sv)); !res) {
        co_return;
    }
}
```

#### 等待接受连接

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpListener;
using ilias::TcpStream;
using ilias::IPEndpoint;

// 处理客户端连接的协程
auto handleClient(TcpStream stream) -> ilias::Task<void> {
    std::array<std::byte, 1024> buffer;
    
    // 读取数据并回显
    while (true) {
        auto n = co_await stream.read(buffer);
        if (!n || n == 0) {
            break;
        }
        co_await stream.write(ilias::makeBuffer(buffer.data(), *n));
    }
}

void ilias_main() {
    auto listener = (co_await TcpListener::bind("127.0.0.1:8080")).value();
    
    while (true) {
        auto [stream, endpoint] = (co_await listener.accept()).value();
        // 为每个客户端启动一个新协程
        auto handle = ilias::spawn(handleClient(std::move(stream)));
        // handle 可以用于检查是否完成或等待完成
        // 如果丢弃 handle 则相当于 detach
    }
}
```

### 启动协程

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

void ilias_main() {
    // 启动一个协程
    auto handle = ilias::spawn(task());
    
    // 启动一个阻塞任务,会被提交到线程池
    auto handle2 = ilias::spawnBlocking(callable);
    
    // handle 可以用于检查是否完成或等待完成
    co_await std::move(handle);

    // 如果要更好地控制协程的生命周期,可以使用 TaskScope 或 TaskGroup<T>
}
```

### 错误处理

支持错误码和异常,核心类型为 `Result<T, E>`,是 `std::expected<T, E>` 的别名.  
根据 C++ 版本,会选择使用标准库(C++23)或第三方实现(zeus_expected).

- 异常会逐层传递 从 await点抛出
- `Result<T, E>` 相当于 `std::expected<T, E>`
- `Err<T>` 相当于 `std::unexpected<T>`
- `IoResult<T>` 相当于 `Result<T, std::error_code>`

#### 两种错误处理方式

```cpp
auto example() -> ilias::Task<void> {
    // 方式 1: 使用 value()(错误时会抛异常 最上层 try catch)
    auto stream = (co_await TcpStream::connect("example.com:80")).value();
    
    // 方式 2: 显式检查错误
    auto result = co_await TcpStream::connect("example.com:80");
    if (!result) {
        std::println("连接失败: {}", result.error().message());
        co_return;
    }
    // 使用 *result
}
```

### 和 Qt 的交互

```cpp
#include <ilias/platform/qt.hpp>
#include <QApplication>

auto main(int argc, char **argv) -> int {
    QApplication app(argc, argv);
    ilias::QIoContext ctxt; // 与 Qt 集成的 IO 上下文
    ctxt.install();
    
    // 之后的代码和其他平台一样,可以正常使用协程
    
    return app.exec();
}
```

### 取消

支持取消操作,取消会在 await 点停止执行当前协程,类似于抛出一个不可捕获的异常.

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

using namespace std::literals;

void ilias_main() {
    auto task = []() -> ilias::Task<int> {
        co_return 1;
    };
    auto handle = ilias::spawn(task());
    handle.stop(); // 发送取消信号

    // WaitHandle<T> co_await 的结果为 Option<T> (std::optional 的别名,自动将 void 替换为 std::monostate)
    // 如果是 nullopt,代表任务被取消
    auto res = co_await std::move(handle);
    assert(res == 1); // 因为这个 task 没有 await 点,所以取消不会成功

    // 带有 await 点的任务
    auto task2 = []() -> ilias::Task<int> {
        co_await ilias::sleep(1000ms);
        co_return 1;
    };
    auto handle2 = ilias::spawn(task2());
    handle2.stop();
    auto res2 = co_await std::move(handle2);
    assert(res2 == std::nullopt); // 因为 sleep 是 await 点,取消会成功
}
```

### 小工具

#### whenAny

等待 N 个awaitable任意一个完成,返回 `std::tuple<Option<T1>, Option<T2>, ...>`,其他会被取消并等待取消完成.

```cpp
auto fn() -> ilias::Task<void> {
    auto [a, b] = co_await whenAny(taskA(), taskB());
    if (a) { // taskA() 先完成
        // 使用 *a
    }
    if (b) { // taskB() 先完成
        // 使用 *b
    }
}
```

#### whenAll

等待 N 个awaitable全部完成,返回 `std::tuple<T1, T2, ...>`.

```cpp
auto fn() -> ilias::Task<void> {
    // 只有两个都完成了才会返回
    auto [a, b] = co_await whenAll(taskA(), taskB());
    // 使用 a 和 b
}
```

#### setTimeout

让一个awaitable在指定时间后取消,返回 `Option<T>`.

```cpp
auto fn() -> ilias::Task<void> {
    if (auto res = co_await setTimeout(doJob(), 1s); res) {
        // doJob 在 1s 内完成
    } else {
        // 超时,doJob 被取消
    }
}
```

#### unstoppable

创建一个不可取消的作用域,里面的awaitable不会被取消.

```cpp
auto fn = []() -> ilias::Task<void> {
    co_await unstoppable(sleep(1s));
};

auto example() -> ilias::Task<void> {
    auto handle = ilias::spawn(fn());
    handle.stop(); // 不起作用,sleep 不会被取消
}

// 管道语法
auto example2() -> ilias::Task<void> {
    co_await (doJob() | unstoppable());
}
```

#### finally

保证在awaitable结束时执行(包括抛出异常和取消).

```cpp
auto fn() -> ilias::Task<void> {
    int fd = openFile();
    
    co_await finally(doJob(), [&]() -> ilias::Task<void> {
        // cleanup handler 可以捕获局部变量
        // 保证在执行时这些变量还存活
        closeFile(fd);
        co_return;
    });
}
```

### 同步

支持多种同步工具 Channel、Mutex、TaskGroup.

#### Channel

目前支持 oneshot 和 mpsc 两种类型的通道.

```cpp
auto fn() -> ilias::Task<void> {
    // 创建一个通道
    // 参数 3 是容量大小,如果 send 时超过容量会阻塞
    // oneshot相当于就是容量为 1 的
    auto [sender, receiver] = mpsc::channel(3);
    
    co_await sender.send(1);
    auto val = co_await receiver.recv();
    assert(val == 1);
}
```

#### Mutex

```cpp
auto fn() -> ilias::Task<void> {
    auto mutex = ilias::Mutex {};

    // Mutex加锁的结果是MutexGuard, 析构会自动释放锁
    {
      auto guard = co_await mutex.lock();
      guard.unlock(); // 提早释放
    }

    // 有时候 用户有手动管理锁的需求
    {
        auto guard = co_await mutex.lock()
        guard.leak(); // 把释放的责任交给用户
        mutex.unlockRaw(); // 手动释放锁
    }
}
```

#### TaskGroup

```cpp
auto fn() -> ilias::Task<void> {
    // T 是返回值类型
    // 如果 group 被析构时还有 task,所有 task 会收到取消信号并 detach
    auto group = ilias::TaskGroup<void>();
    group.spawn(taskA());
    group.spawn(taskB());

    // 等待所有任务完成,返回 std::vector<T> (void 会自动变成 std::monostate)
    co_await group.waitAll();

    // 给所有 task 发取消信号,然后等待所有 task 完成并丢弃返回值
    co_await group.shutdown(); 

    // 等待下一个完成的 task,返回 Option<T>
    co_await group.next();
}
```

#### TaskScope

```cpp
auto fn() -> ilias::Task<void> {
    // 函数版本(最安全)
    auto val = co_await TaskScope::enter([](auto &scope) -> ilias::Task<int> {
        scope.spawn(another()); // 可以在 scope 里启动其他 task
        co_return 42;
    });
    // 离开 scope 时保证所有 task 已完成
    assert(val == 42);

    // 对象版本(当你想把 scope 放在类成员里时)
    TaskScope scope;
    scope.spawn(another());

    // 由于 C++ 没有异步析构器,必须在 scope 析构前保证 scope 是空的
    // 否则会直接 abort,推荐放在 finally 里面
    co_await scope.waitAll();
}
```

### 依赖

- **OpenSSL** (可选,用于 非Windows的平台上 TLS 支持)
- **liburing** (可选,被 UringContext 使用)

### 后端

| 后端 | 平台 | 状态 | 最低要求 |
|------|------|------|----------|
| epoll | Linux | ✅ 已完成 | Linux 4.3+ |
| IOCP | Windows | ✅ 已完成 | Windows 7+ |
| io_uring | Linux | ✅ 已完成 | Linux 5.1+ |
| Qt | 跨平台 | ✅ 已完成 | Qt 6+ |

### 系统要求

- **Windows**: 7+ (使用了afd)
- **Linux**: 4.3+ (起码要epoll)

#### 编译器支持

- **GCC**: 11+
- **Clang**: 15+ (需要CTAD for alias)
- **MSVC**: (Visual Studio 2022)

#### C++ 标准

- **C++20** 或更高

#### 构建系统

- **xmake** (推荐)
- **cmake** (TODO)

### 已知限制

- 目前仅支持 Linux 和 Windows
- macOS 支持计划中 (但我没有macOS设备)

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

## 📄 许可证

本项目采用 [MIT 许可证](LICENSE)

---

**Star ⭐ 这个项目如果你觉得有用！**
