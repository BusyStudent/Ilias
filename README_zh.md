# Ilias

## 一个纯头的cpp的异步网络库 基于cpp20的无栈协程

### [English](README.md) | [中文](README_zh.md)

### CI 状态

| CI Name | Status |
| --------- | ------- |
| Windows  | [![CI for windows by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml) |
| Linux    | [![CI for linux by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml) |
| Coverage | [![codecov](https://codecov.io/gh/BusyStudent/Ilias/graph/badge.svg?token=W9MQGIPX6F)](https://codecov.io/gh/BusyStudent/Ilias)|

## 快速使用

### 最基本的环境

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt; // 首先 先构建一个上下文 用于提交任务 这是thread_local的 每个线程只有一个
    // 目前可用的Io上下文有 IocpContext EpollContext UringContext QIoContext
    // 没有Io的简单执行器有 MiniExecutor
    // PlatformContext 是个别名 会根据当前编译时候时候的平台来选择
    auto task = []() -> ilias::Task<int> { // 这是协程 返回值需要是 Task<T>
        co_return 1;
    };
    auto result = task().wait(); // 创建任务 并堵塞等待完成
    if (result) { // 协程Task<T>的返回值都为 Result<T> 相当于 std::expected<T, Error>
        std::cout << "result: " << *result << std::endl;
    }
    return 0;
}
```

### Socket

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt;
    auto task = [&]() -> ilias::Task<void> {
        ilias::TcpClient client(ctxt, AF_INET);
        ilias::IPEndpoint ep("127.0.0.1", 8080);
        if (auto ret = co_await client.connect(ep); !ret) {
            co_return ilias::Unexpected(ret.error());
        }
        // ilias::makeBuffer 会把任何可以到 std::span<T>的类给 转化成 std::span<const std::byte> 或者 std::span<std::byte>
        // read 和 write 的参数都为 std::span<byte>
        std::string_view sv = "HELLO WORLD";
        if (auto ret = co_await client.write(ilias::makeBuffer(sv)); !ret) {
            co_return ilias::Unexpected(ret.error());
        }
        co_return {};
    };
    if (auto ret = task().wait(); !ret) {
        std::cout << "error: " << ret.error().toString() << std::endl;
    }
    return 0;
}
```

### Http Client

简单的HTTP请求 支持GET POST HEAD等

```cpp
#include <ilias/platform.hpp>
#include <ilias/http.hpp>
#include <ilias/task.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt;
    ilias::HttpSession session(ctxt);
    auto task = [&]() -> ilias::Task<void> {
        auto reply = (co_await session.get("https://www.google.com")).value();
        auto content = (co_await reply.text()).value();
        std::cout << "Http to " << reply.url().toString() << " Status code"  << reply.statusCode() << std::endl;
        std::cout << content << std::endl;
        co_return {};
    };
    task().wait();
    return 0;
}
```

### 错误处理

支持错误码和异常, 类为```Result<T>``` 是```Expected<T, Error>``` 的别名  
而```Expected<T>``` 会根据当前标准库的实现 选择使用标准库(cpp23) 还是内建的实现

而 ```Error``` 相当于 ```std::error_code``` 你可以自己定义错误码 只需要实现自己的 ```ErrorCategory``` 然后用```ILIAS_DECLARE_ERROR(errc, category)``` 绑定一下就行, 不然就需要手动构造 ```Error``` 对象 如 ```Error(yourCode, categoryReference)```

```cpp

auto fn() -> Task<void> {
    auto ret = co_await someTask();
    ret.value(); // 如果没有数值 会抛出BadExpectedAccess<Error>， Task<T> 会自动把BadExpectedAccess<Error> 转化为 Error 存在返回值里面
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
    // 或者直接不管 错误会向上层传递
    ret.value();

    // 普通的异常 会逐层向上传递 不会被Task<T>内部捕获
    throw 1; //< 异常会在外部 co_await fn();的地方重新抛出
}
```

## 同步

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
    co_return {};
}
```

- whenAll

```cpp
auto fn() -> Task<void> {
    // 只有两个都完成了 才会返回
    auto [a, b] = co_await whenAll(taskA(), taskB());
    // use a & b
    co_return {};
}
```

- Channel

```cpp
auto fn() -> Task<void> {
    // 创建一个通道
    auto [sender, receiver] = mpmc::channel<int>(3); // 3 是容量大小 如果send时候将要超容量 会堵塞 如果不填 默认是 size_t::max() 相当于无限大
    co_await sender.send(1);
    auto val = co_await receiver.recv();
    co_return {};
}
```

### 依赖

- zlib (可选)
- openssl (可选)
- liburing (可选, 被 UringContext使用)

### 后端

| Backend | Progress | Description |
| --------- | ---------- | ------------- |
| epoll     | Completed | Linux only |
| IOCP      | Completed | Windows only |
| Qt        | Completed | Current only support socket |
| io_uring  | Completed | Linux only |
