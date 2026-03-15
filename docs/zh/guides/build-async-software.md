---
outline: deep
---

# 使用 Ilias 构建异步软件

这份指南不是 API 列表，而是一份面向实际工程的“落地说明书”。目标是回答一个问题：**如何用 Ilias 把一个真实的异步程序从 0 搭起来，并且保持可维护、可取消、可扩展。**

## 1. 先建立正确认知

Ilias 是一个基于 C++20 协程的 completion-based 异步运行时。你可以把它理解成：

- `Task<T>`：异步函数的返回值
- `PlatformContext`：当前线程上的事件循环 / I/O 调度器
- `IoTask<T>`：会返回 `Result<T, std::error_code>` 的 I/O 任务
- `TaskScope` / `TaskGroup`：结构化并发与任务治理工具
- `BufStream<T>` / `File` / `TcpStream` / `TlsStream<T>`：统一风格的 I/O 对象

如果你以前习惯 callback、signal-slot 链式调用或者大量 `shared_ptr` 管生命周期，那么 Ilias 的核心收益就是：

1. 顺序代码风格写异步逻辑
2. 用 stop token 做取消
3. 用作用域管理任务生命周期
4. 用统一流接口复用协议代码

## 2. 第一步：启动运行时

每个线程先装一个执行器。最常见的是 `PlatformContext`。

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto app() -> ilias::Task<int> {
    co_return 0;
}

auto main() -> int {
    ilias::PlatformContext ctxt;
    ctxt.install();
    return app().wait();
}
```

如果是普通控制台应用，更简洁的写法是：

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

int ilias_main() {
    co_return 0;
}
```

### 什么时候用别的上下文？

- 默认跨平台程序：`PlatformContext`
- 纯任务测试：`EventLoop`
- Qt GUI：`QIoContext`
- 平台专门调优：`IocpContext` / `EpollContext` / `UringContext`

## 3. 第二步：用 `Task<T>` 组织业务逻辑

异步函数的基本形式：

```cpp
auto calc() -> ilias::Task<int> {
    co_await ilias::sleep(std::chrono::milliseconds(10));
    co_return 42;
}
```

### 选择返回类型的建议

- 普通异步逻辑：`Task<T>`
- 可能有 I/O 错误：`IoTask<T>`
- 流式产生多个值：`Generator<T>`

### 错误处理的两种方式

```cpp
auto x = (co_await someIo()).value();
```

适合 demo 和顶层入口。

```cpp
auto res = co_await someIo();
if (!res) {
    co_return ilias::Err(res.error());
}
```

适合库代码、服务端、需要明确失败路径的逻辑。

如果返回的是 `Result<T, E>`，可以用：

```cpp
auto value = ILIAS_CO_TRY(co_await someIo());
```

## 4. 第三步：把 I/O 当成统一的“流”来设计

Ilias 的一个很强的设计点是：很多对象都遵循类似的读写接口。

常见流对象：

- `TcpStream`
- `TlsStream<T>`
- `File`
- `BufStream<T>`
- `DuplexStream`
- `Stdin` / `Stdout`

因此协议代码通常可以写得非常统一。

### 4.1 最基本的 socket 客户端

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

void ilias_main() {
    auto stream = (co_await ilias::TcpStream::connect("127.0.0.1:8080")).value();
    (co_await stream.writeAll(ilias::makeBuffer("hello\n"))).value();
}
```

### 4.2 文本协议优先加 `BufStream`

```cpp
auto run() -> ilias::Task<void> {
    auto tcp = (co_await ilias::TcpStream::connect("127.0.0.1:8080")).value();
    auto stream = ilias::BufStream{std::move(tcp)};

    (co_await stream.writeAll(ilias::makeBuffer("PING\r\n"))).value();
    (co_await stream.flush()).value();

    auto line = (co_await stream.getline("\r\n")).value();
    co_return;
}
```

适合：

- HTTP
- SMTP / POP3 / IMAP
- Redis RESP
- 行分隔协议
- 自定义文本 RPC

### 4.3 二进制定长协议用 `readAll`

```cpp
std::array<std::byte, 8> header;
auto ok = co_await stream.readAll(header);
if (!ok) {
    co_return;
}
```

`read()` 不保证读满，协议实现里不要误用。

## 5. 第四步：用结构化并发管理任务

这是写真实异步软件时最关键的一步。

## 5.1 什么时候直接 `spawn()`？

适合：

- 简短后台任务
- demo
- 生命周期不重要的临时任务

```cpp
auto handle = ilias::spawn(worker());
co_await std::move(handle);
```

但一旦程序进入“服务端 / GUI / 会话 / 子系统”规模，**直接裸 `spawn()` 很快会让生命周期失控**。

## 5.2 优先使用 `TaskScope`

`TaskScope` 非常适合“这些任务属于某个父作用域”的情况。

```cpp
auto runServer() -> ilias::Task<void> {
    auto listener = (co_await ilias::TcpListener::bind("127.0.0.1:8080")).value();

    co_await ilias::TaskScope::enter([&](ilias::TaskScope &scope) -> ilias::Task<void> {
        while (true) {
            auto accepted = co_await listener.accept();
            if (!accepted) {
                co_return;
            }
            auto [stream, peer] = std::move(*accepted);
            scope.spawn(handleClient(std::move(stream), peer));
        }
    });
}
```

这样做的收益：

- 所有连接处理协程都被父作用域托管
- 停止时可以统一 stop + wait
- 不需要每个连接都手动记住 handle

## 5.3 什么时候用 `TaskGroup<T>`？

如果你关心“一批任务的结果”，用 `TaskGroup<T>` 更顺手：

```cpp
auto fetchAll() -> ilias::Task<void> {
    ilias::TaskGroup<int> group;
    for (int i = 0; i < 10; ++i) {
        group.spawn(fetchOne(i));
    }

    auto results = co_await group.waitAll();
    co_return;
}
```

适合：

- 并发抓取多个资源
- 并行探测多个节点
- 一批任务完成后统一汇总

## 6. 第五步：设计取消与优雅退出

如果没有取消设计，再漂亮的异步代码最终也会在退出路径出问题。

## 6.1 顶层退出：`signal::ctrlC()`

```cpp
void ilias_main() {
    auto [server, stop] = co_await ilias::whenAny(
        runServer(),
        ilias::signal::ctrlC()
    );

    if (stop) {
        // 在这里记录日志、触发资源释放
    }
}
```

## 6.2 在协程内部观察停止请求

```cpp
auto worker() -> ilias::Task<void> {
    auto token = co_await ilias::this_coro::stopToken();
    while (!token.stop_requested()) {
        co_await ilias::sleep(std::chrono::milliseconds(100));
    }
}
```

## 6.3 用 `finally()` 保证异步清理

```cpp
co_await ilias::finally(
    runTransaction(),
    rollbackOrCleanup()
);
```

或者

``` cpp
co_await finally(
    mainTask(),
    []()  -> Task<void> {
        // do sthing
    }
);
```

适合：

- socket 关闭前发送协议尾包
- 文件 flush / commit
- 事务回滚
- 子任务退出前登记状态

## 6.4 某些任务必须执行完：`unstoppable()`

```cpp
co_await ilias::unstoppable(flushAndCommit());
```

当你不希望外部 stop 请求打断这段关键收尾逻辑时很有用。

## 7. 第六步：正确处理阻塞逻辑

异步软件常见错误不是 I/O 写错，而是把阻塞逻辑偷偷塞进协程线程。

### 错误示例

```cpp
std::this_thread::sleep_for(1s);
auto text = parseHugeFileSynchronously();
```

### 正确替代

#### 方案 1：`blocking()`

```cpp
auto text = co_await ilias::blocking([] {
    return parseHugeFileSynchronously();
});
```

#### 方案 2：`spawnBlocking()`

```cpp
auto handle = ilias::spawnBlocking([] {
    return heavyWork();
});

auto result = co_await std::move(handle);
```

#### 方案 3：`Thread`

```cpp
auto thread = ilias::Thread([]() -> ilias::Task<int> {
    co_return 42;
});

auto result = co_await thread.join();
```

### 如何选？

- 偶发短阻塞：`blocking()`
- 后台阻塞任务：`spawnBlocking()`
- 需要独立线程生命周期：`Thread`

## 8. 第七步：加入同步原语和消息通道

## 8.1 `Mutex` / `Locked<T>`

当多个协程共享状态时：

```cpp
ilias::Locked<std::vector<int>> values;
auto guard = co_await values.lock();
guard->push_back(1);
```

适合：

- 共享缓存
- 会话表
- 内存状态机

## 8.2 `Semaphore`

控制并发度：

```cpp
ilias::Semaphore sem(100);
auto permit = co_await sem.acquire();
```

适合：

- 限流
- 连接池
- 外部 API 并发上限

## 8.3 `mpsc::channel<T>()`

实现 actor / 消息驱动非常方便：

```cpp
auto [tx, rx] = ilias::mpsc::channel<int>(128);
co_await tx.send(42);
auto value = co_await rx.recv();
```

适合：

- 单线程 actor
- worker 池任务分发
- UI/网络/存储子系统解耦

## 8.4 `Oneshot`

适合一次性交付结果，比如“启动完成通知”或“单次回应”。

## 9. 第八步：加入文件、TLS、进程能力

## 9.1 文件 I/O

```cpp
auto file = (co_await ilias::File::open(
    "./data.txt",
    ilias::OpenOptions{}.write().create().truncate()
)).value();

(co_await file.writeAll(ilias::makeBuffer("hello\n"))).value();
(co_await file.flush()).value();
```

建议：

- 顺序读写用 `read()` / `write()`
- 并发随机读写用 `pread()` / `pwrite()`

## 9.2 TLS

```cpp
auto tcp = (co_await ilias::TcpStream::connect("example.com:443")).value();
ilias::TlsContext tls;
auto ssl = ilias::TlsStream{tls, std::move(tcp)};
ssl.setHostname("example.com");
(co_await ssl.handshake(ilias::TlsRole::Client)).value();
```

之后就可以把 `ssl` 当普通流继续包上 `BufStream`。

## 9.3 子进程

```cpp
auto output = co_await ilias::Process::Builder{"powershell"}
    .args({"-Command", "Get-ChildItem"})
    .output();
```

适合：

- 启动外部工具
- 读取命令输出
- 与已有 CLI 工具链组合

## 10. 一个完整服务端的推荐骨架

下面给出一个更贴近真实项目的骨架：

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <ilias/io.hpp>
#include <ilias/signal.hpp>

using namespace std::literals;

auto handleClient(ilias::TcpStream stream) -> ilias::Task<void> {
    auto io = ilias::BufStream{std::move(stream)};
    while (true) {
        auto line = co_await io.getline("\n");
        if (!line) {
            co_return;
        }
        if (!(co_await io.writeAll(ilias::makeBuffer(*line)))) {
            co_return;
        }
        if (!(co_await io.flush())) {
            co_return;
        }
    }
}

auto serve() -> ilias::Task<void> {
    auto listener = (co_await ilias::TcpListener::bind("127.0.0.1:8080")).value();

    co_await ilias::TaskScope::enter([&](ilias::TaskScope &scope) -> ilias::Task<void> {
        while (true) {
            auto accepted = co_await listener.accept();
            if (!accepted) {
                co_return;
            }
            auto [stream, peer] = std::move(*accepted);
            scope.spawn(handleClient(std::move(stream)));
        }
    });
}

void ilias_main() {
    auto [server, stop] = co_await ilias::whenAny(serve(), ilias::signal::ctrlC());
    if (stop) {
        // graceful shutdown log
    }
}
```

这个骨架已经包含了构建异步软件最重要的几个元素：

- 平台运行时
- 统一 I/O 接口
- 结构化并发
- 优雅退出

## 11. 给真实项目的落地建议

### 11.1 把系统按边界拆成几个异步子系统

推荐按职责拆：

- 网络层
- 协议层
- 状态层
- 存储层
- 后台任务层

然后通过：

- `TaskScope`
- `mpsc::channel<T>()`
- `oneshot::channel<T>()`
- `Semaphore`
- `Process`
- `Thread`

把它们连接起来。

### 11.2 先设计生命周期，再设计接口

写 Ilias 程序时，先回答：

- 这个任务属于谁？
- 谁负责停止它？
- 停止时要不要清理？
- 结果由谁收集？

通常答案会自然对应：

- 属于某个父对象：`TaskScope`
- 需要收集结果：`TaskGroup<T>`
- 必须清理：`finally()`
- 必须完成：`unstoppable()`

### 11.3 统一约定错误风格

在一个项目里最好统一：

- 顶层 demo / main：允许 `.value()`
- 库代码 / 服务端：显式检查 `Result`

这样代码风格会更稳定。

## 12. 常见坑

### 坑 1：忘记安装上下文

没有 `ctxt.install()`，很多 task / I/O API 都无法正常工作。

### 坑 2：顺序流并发访问

`File::read/write` 和某些流的顺序接口会推进内部 offset，不适合并发直接共享。

### 坑 3：把 `spawn()` 当万能解法

`spawn()` 很方便，但生命周期很容易散掉。稍微复杂一点的系统优先 `TaskScope`。

### 坑 4：没有超时与退出路径

真实网络软件一定要考虑：

- timeout
- cancellation
- shutdown
- cleanup

`whenAny + sleep` 或 `setTimeout()` 是第一批就该接入的机制。

## 13. 一句话总结

如果你希望用 Ilias 写出稳定的异步软件，请记住这条主线：

> 先装运行时，再用 `Task` 写逻辑，用统一流写 I/O，用 `TaskScope/TaskGroup` 管并发，用 stop token 与 `finally()` 管退出。

掌握这条主线后，无论你是写控制台工具、TCP 服务、TLS 客户端、Qt 集成程序还是多任务后台系统，整体方法都会非常一致。
