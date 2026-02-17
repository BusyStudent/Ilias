---
outline: deep
---

# 快速开始

## 加入依赖

首先选择构建系统 ilias同时支持xmake和cmake两套构建系统

::: tip
Xmake 是主力构建系统 维护的更加频繁 推荐优先使用
:::

### 使用 xmake

```lua
add_repositories("btk-repo https://github.com/Btk-Project/xmake-repo.git")
add_requires("ilias")

target("your_app")
    add_packages("ilias")
```

### 使用 cmake

``` cmake
include(FetchContent)

FetchContent_Declare(
    ilias
    GIT_REPOSITORY https://github.com/BusyStudent/Ilias.git
    GIT_TAG main
)

FetchContent_MakeAvailable(ilias)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ilias::ilias)
```

## 编写第一个程序

先来介绍一个非常简单的例子

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto task() -> ilias::Task<int> {
    co_return 0;
}

auto main(int argc, char **argv) -> int {
    ilias::PlatformContext ctxt {}; // 创建一个用于调度的执行器 
    ctxt.install(); // 然后安装到thread_local存储中

    return task().wait(); // 创建任务 堵塞等待
}
```

这里面用到了两个大模块

- task 提供主要的无栈协程API 包括```Task```, ```Generator```, ```TaskGroup```, ```spawn``` 等
- platform 提供平台相关的执行器API 包括```IocpContext```, ```EpollContext```, ```UringContext```, ```PlatformContext```是一个alias

platform还提供一个辅助宏来简化main的编写

```cpp
void ilias_main() {
    co_return;
}

```

## 编写一个异步网络程序

上面的程序啥都没干 显得有点无趣 我们给他加上点东西

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

// 注意 所有可能失败的函数都返回Result<T, E> (std::expected<T, E>)
// 在这个例子中偷懒直接使用.value() 拆开
using namespace std::literals;

auto handle(ilias::TcpStream stream) -> ilias::Task<void> {
    auto content = ilias::makeBuffer("hello world\n"sv);

    (co_await stream.writeAll(content)).value();
    (co_await stream.flush()).value();
}
void ilias_main() {
    // 先创建一个监听器
    auto listener = (co_await ilias::TcpListener::bind("127.0.0.1:8080")).value();
    while (true) {
        auto [stream, endpoint] = (co_await listener.accept()).value();

        // 创建一个子任务来处理连接, 如果要更好的管理 可以使用TaskScope
        ilias::spawn(handle(std::move(stream)));
    }
}
```

发现让程序这样死循环着有点不爽 我们给他加上一个退出信号

```cpp
#include <ilias/platform.hpp>
#include <ilias/signal.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <print>

// 和上面的代码一样
auto handle(ilias::TcpStream stream) -> ilias::Task<void>;
auto accept(ilias::IPEndpoint endpoint) -> ilias::Task<void>;

void ilias_main() {
    auto [_, ctrlc] = co_await ilias::whenAny(
        accept("127.0.0.1:8080"),
        ilias::signal::ctrlC(),
    );
    if (ctrlc) {
        std::println("服务器正在退出...");
    }
}

```
