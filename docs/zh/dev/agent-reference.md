---
outline: deep
---

# Agent 参考手册

这份文档面向会阅读和修改 Ilias 项目的 agent、自动化工具与协作者。目标不是重复 API 注释，而是给出一份“如何快速理解项目、如何安全地写出符合项目风格的异步代码”的工作手册。

## 1. 项目入口与模块地图

### 1.1 推荐入口头文件

- `#include <ilias.hpp>`：导入绝大多数公开模块，适合原型和阅读示例。
- `#include <ilias/platform.hpp>`：平台上下文与 `ilias_main`。
- `#include <ilias/task.hpp>`：栈式为 0 的主协程 API。
- `#include <ilias/io.hpp>`：通用流、缓冲流、stdio、双工流。
- `#include <ilias/net.hpp>`：TCP / UDP / 地址解析 / socket 选项。
- `#include <ilias/fs.hpp>`：文件与 pipe。
- `#include <ilias/sync.hpp>`：互斥、事件、信号量、oneshot、MPSC。
- `#include <ilias/tls.hpp>`：TLS 上下文与 TLS 流。
- `#include <ilias/process.hpp>`：异步子进程。
- `#include <ilias/signal.hpp>`：Ctrl-C / signal 等退出信号。

### 1.2 模块职责

| 模块 | 主要头文件 | 核心职责 |
| --- | --- | --- |
| platform | `ilias/platform.hpp` | 选择平台执行器：Windows 默认 IOCP，Linux 默认 epoll，可选 io_uring / Qt |
| runtime | `ilias/runtime/*` | 执行器、取消 token、协程上下文、timer 等底层设施 |
| task | `ilias/task.hpp` | `Task<T>`、`Generator<T>`、`spawn`、`whenAll`、`whenAny`、`TaskGroup`、`TaskScope` |
| io | `ilias/io.hpp` | 流抽象、`BufReader` / `BufWriter` / `BufStream`、I/O 扩展方法 |
| net | `ilias/net.hpp` | `TcpStream`、`TcpListener`、`UdpSocket`、`AddressInfo`、endpoint |
| fs | `ilias/fs.hpp` | `File`、`OpenOptions`、pipe |
| sync | `ilias/sync.hpp` | `Mutex`、`Event`、`Semaphore`、`oneshot::channel<T>()`、`mpsc::channel<T>()` |
| tls | `ilias/tls.hpp` | `TlsContext`、`TlsStream<T>` |
| process | `ilias/process.hpp` | `Process::Builder`、`spawn()`、`output()` |
| signal | `ilias/signal.hpp` | `signal::ctrlC()`，Linux 下还有通用 `signal(int)` |

## 2. Ilias 的核心心智模型

### 2.1 一个线程一个调度器

Ilias 的默认使用方式是：**每个线程安装一个执行器 / I/O 上下文**，当前线程上的协程都在这个执行器里被调度。

常见上下文：

- `PlatformContext`：跨平台默认选择，通常是最先用的类型。
- `EventLoop`：纯执行器，不带 I/O 后端，适合测试、纯 task 逻辑。
- `QIoContext`：与 Qt 事件循环整合。
- `IocpContext` / `EpollContext` / `UringContext`：平台具体实现。

### 2.2 completion-based

大部分操作返回 awaitable：

- 普通无错误任务：`Task<T>`
- 可能失败的 I/O 任务：`IoTask<T>`，本质上是 `Task<Result<T, std::error_code>>`

因此你应优先写：

```cpp
auto run() -> ilias::IoTask<void> {
    auto stream = ILIAS_CO_TRY(co_await ilias::TcpStream::connect("127.0.0.1:8080"));
    co_await stream.writeAll(ilias::makeBuffer("hello"));
}
```

而不是把状态拆成多层回调。

### 2.3 停止是“请求”，不是强杀

Ilias 的取消模型基于 stop token：

- 父任务发出 stop request。
- 子任务、等待器或 I/O 操作在合适时机观察到停止请求。
- 某些 awaiter 会把任务标记为 stopped，而不是抛异常。
- 所以在await点才会被取消

常用接口：

- `co_await this_coro::stopToken()`
- `co_await this_coro::isStopRequested()`
- `co_await this_coro::stopped()`
- `handle.stop()` / `group.stop()` / `scope.stop()`

### 2.4 结构化并发优先

虽然可以直接 `spawn()`，但 Ilias 更鼓励使用：

- `TaskScope`：保证作用域退出前子任务完成。
- `TaskGroup<T>`：管理一批同类型子任务，并逐个收集结果。

如果 agent 在实现服务端逻辑、连接生命周期、资源清理时没有明确理由，优先选这两个而不是裸 `spawn()`。

## 3. 代码应该遵循的基本模式

### 3.1 启动运行时

最标准的最小程序：

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

ilias::Task<int> app() {
    co_return 0;
}

auto main() -> int {
    ilias::PlatformContext ctxt;
    ctxt.install();
    return app().wait();
}
```

也可以直接使用：

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

int ilias_main() {
    co_return 0;
}
```

### 3.2 返回类型选择

- 没有错误通道：`Task<T>`
- 有 I/O 错误：`IoTask<T>`
- 流式产出：`Generator<T>` 或 `IoGenerator<T>`
- fire-and-forget：只在非常明确的场景下使用 `FireAndForget`

### 3.3 错误处理风格

项目同时支持两种风格：

1. `.value()`：错误时抛异常，适合样例与顶层逻辑。
2. 显式检查 `Result`：适合服务端和库代码。

库代码与复杂业务里，推荐优先显式检查：

```cpp
auto res = co_await stream.read(buf);
if (!res) {
    co_return ilias::Err(res.error());
}
```

对 `Result` / `Option` 风格协程，优先考虑 `ILIAS_CO_TRY(...)`，它等价于协程里的 `?`。

### 3.4 不要在协程里直接做阻塞调用

以下操作不要直接在协程线程做：

- 长时间 CPU 计算
- `std::this_thread::sleep_for`
- 阻塞文件解析 / 压缩 / 数据库驱动调用
- 第三方同步网络库

替代方案：

- `co_await blocking(fn)`：把阻塞 callable 包装成 awaitable。
- `spawnBlocking(fn)`：提交到线程池，拿到可等待 handle。
- `Thread(fn, ...)`：给任务一个完整独立线程生命周期。

### 3.5 优先通过扩展方法写流逻辑

凡是满足 `Readable` / `Writable` / `BufReadable` / `BufWritable` 的流，优先使用统一扩展方法：

- `readAll`
- `writeAll`
- `readToEnd`
- `getline`
- `copy`
- `flush`
- `writeString`
- 各种整数读写方法

这样一套代码常能直接应用到：

- `TcpStream`
- `TlsStream<T>`
- `File`
- `BufStream<T>`
- `DuplexStream`
- `Stdin` / `Stdout`

## 4. 公开接口速查

## 4.1 task 模块

### `Task<T>`

最核心的协程返回类型。

常见操作：

- `co_await task`
- `task.wait()`：阻塞当前线程直到完成
- `blockingWait(task)`：测试或同步边界用

### `spawn()` / `spawnBlocking()`

- `spawn(task)`：把协程作为后台任务启动，返回可 stop / wait 的 handle。
- `spawnBlocking(fn)`：在线程池里运行阻塞 callable。

适用场景：

- 短生命周期后台任务
- listener 为每个连接派生任务
- 需要拿到 stop handle 的 detached 风格任务

### `whenAll()` / `whenAny()`

- `whenAll(a, b, c)`：并发等待全部完成。
- `whenAny(a, b, c)`：谁先完成取谁，并停止其它分支。
- 支持 tuple 形式与 sequence 形式。
- 运算符语法：`a || b` 等价于 `whenAny(a, b)`。

常见用途：

- 并发请求多个子资源
- server 主循环与 `signal::ctrlC()` 竞争
- timeout 组合

### `setTimeout()` / `unstoppable()` / `finally()` / `fmap()` / `scheduleOn()`

这些组合工具在 `task/utils.hpp` 中：

- `setTimeout(task, 5s)`：超时返回 `Option<T>`，超时得到 `nullopt`
- `unstoppable(task)`：忽略上层 stop 请求直到内部任务结束
- `finally(task, cleanup)`：无论正常、停止还是异常都执行异步清理
- `fmap(task, fn)`：映射结果
- `scheduleOn(task, exec)`：把 awaitable 调度到指定执行器

### `TaskGroup<T>`

适合“动态数量 + 同类型结果”的子任务集合：

- `group.spawn(task)`
- `group.next()`：取下一个完成的结果
- `group.waitAll()`：等待所有任务完成
- `group.shutdown()`：stop 后等待清理

### `TaskScope`

适合有明确父子生命周期的并发结构：

- `scope.spawn(...)`
- `scope.spawnBlocking(...)`
- `scope.waitAll()`
- `scope.shutdown()`
- `TaskScope::enter(fn)`：推荐的结构化入口

使用原则：

- 当任务属于某个对象、某个请求、某个连接时，用 `TaskScope`
- 当你要收集一批同类任务的完成结果时，用 `TaskGroup`

### `Thread`

`Thread` 是普通 OS 线程包装 + Executor，在线程中跑一个 Ilias 任务。

常见用法：

```cpp
auto thread = ilias::Thread([]() -> ilias::Task<int> {
    co_return 42;
});

auto result = co_await thread.join();
```

如果需要指定执行器类型：

```cpp
auto thread = ilias::Thread(ilias::useExecutor<ilias::EventLoop>(), fn);
```

### `this_coro` 工具

最重要的一组上下文访问 API：

- `this_coro::stopToken()`
- `this_coro::isStopRequested()`
- `this_coro::executor()`
- `this_coro::yield()`
- `this_coro::stopped()`
- `this_coro::stacktrace()`

## 4.2 io 模块

### 统一流接口

典型流对象会暴露：

- `read(MutableBuffer)`
- `write(Buffer)`
- `flush()`
- `shutdown()`（如果适用）
- `close()` / `cancel()`（如果对象支持）

### `BufReader<T>` / `BufWriter<T>` / `BufStream<T>`

这是实际项目里很常用的高层封装：

- 文本协议、HTTP、行协议：优先 `BufReader` / `BufStream`
- 频繁小块输出：优先 `BufWriter`
- `BufStream<T>` 同时提供读写缓冲

示例：

```cpp
auto stream = ilias::BufStream{std::move(tcp)};
auto line = ILIAS_CO_TRY(co_await stream.getline("\r\n"));
ILIAS_CO_TRY(co_await stream.writeAll(ilias::makeBuffer(line)));
ILIAS_CO_TRY(co_await stream.flush());
```

### stdio 与内存流

项目内也有：

- `Stdin` / `Stdout` / `Stderr`
- `DuplexStream`

这意味着 agent 写 demo、测试或协议解析逻辑时，不一定需要真实 socket。

## 4.3 net 模块

### `TcpStream`

用于面向连接流式通信。

常见接口：

- `TcpStream::connect(endpoint)`
- `read` / `write` / `flush`
- `shutdown()`
- `localEndpoint()` / `remoteEndpoint()`
- `setOption()` / `getOption()`
- `poll()`

### `TcpListener`

用于监听 TCP 连接。

常见接口：

- `TcpListener::bind(endpoint)`
- `TcpListener::bind(endpoint, backlog, configureFn)`
- `accept()`
- `localEndpoint()`

一个惯用写法是 accept loop + `TaskScope`：

```cpp
auto serve(ilias::IPEndpoint ep) -> ilias::Task<void> {
    auto listener = (co_await ilias::TcpListener::bind(ep)).value();
    co_await ilias::TaskScope::enter([&](ilias::TaskScope &scope) -> ilias::Task<void> {
        while (true) {
            auto [stream, peer] = (co_await listener.accept()).value();
            scope.spawn(handleClient(std::move(stream), peer));
        }
    });
}
```

### `UdpSocket`

用于数据报通信。

常见接口：

- `UdpSocket::bind(endpoint)`
- `sendto()` / `recvfrom()`
- 也支持 connect 后按流式方式收发

### `AddressInfo`

异步 DNS / 地址解析入口：

- `AddressInfo::fromHostname(hostname)`
- `AddressInfo::fromHostname(hostname, service)`
- `endpoints()` 拿到 `std::vector<IPEndpoint>`

## 4.4 fs 模块

### `OpenOptions`

文件打开策略对象，支持链式配置：

- `read()`
- `write()`
- `append()`
- `truncate()`
- `create()`
- `createNew()`

预定义常量：

- `OpenOptions::ReadOnly`
- `OpenOptions::WriteOnly`
- `OpenOptions::ReadWrite`

### `File`

统一文件流对象：

- `File::open(path, options)`
- `read` / `write`
- `pread` / `pwrite`
- `seek` / `tell`
- `size`
- `truncate`
- `flush`
- `readToEnd`

注意事项：

- `read()` / `write()` 会推进内部 offset，不要并发调用。
- 并发随机读写优先 `pread()` / `pwrite()`。

## 4.5 sync 模块

### `Mutex` / `Locked<T>`

- `co_await mutex.lock()` 或 `co_await mutex`
- `MutexGuard` 自动解锁
- `Locked<T>` 把数据与锁绑定在一起，减少裸共享状态

### `Event`

适合“一个或多个等待者等待某个条件发生”。

### `Semaphore`

适合限制并发度，比如最多 100 个并发任务访问某资源。

### `channel<T>()`

MPSC 通道：

- `auto [tx, rx] = mpsc::channel<T>(capacity);`
- `co_await tx.send(value)`
- `co_await rx.recv()`
- 适合任务间消息驱动架构

Oneshot 通道:

- `auto [tx, rx] = oneshot::channel<T>();`
- `co_await tx.send(value)`
- `co_await rx.recv()`
- 适合单次结果传递，通常是 request/response 或启动同步。

## 4.6 tls 模块

### `TlsContext`

配置 TLS 验证与证书：

- `setVerify(bool)`
- `loadDefaultRootCerts()`
- `loadRootCertsFile()` / `loadRootCerts()`
- `useCertFile()` / `useCert()`
- `usePrivateKeyFile()` / `usePrivateKey()`

### `TlsStream<T>`

把任意底层流包装成 TLS 流：

- `TlsStream{ctxt, std::move(stream)}`
- `setHostname()`
- `setAlpnProtocols()`
- `handshake(TlsRole::Client/Server)`
- 之后像普通流一样 `read` / `write` / `flush` / `shutdown`

## 4.7 process 与 signal

### `Process::Builder`

当前公开接口主要支持：

- `args(...)`
- `cin(...)` / `cout(...)` / `cerr(...)`
- `spawn()`
- `output()`

适合 agent 做：

- 启动子进程
- 抓 stdout / stderr
- 与 pipe 组合实现异步进程通信

### `signal::ctrlC()`

做控制台程序优雅退出时非常常用：

```cpp
auto [server, stop] = co_await ilias::whenAny(runServer(), ilias::signal::ctrlC());
if (stop) {
    // graceful shutdown
}
```

## 5. 给 agent 的实践规则

### 5.1 选型规则

1. **一个顶层入口**：先装 `PlatformContext`。
2. **纯业务协程**：返回 `Task<T>`。
3. **I/O 协程**：返回 `IoTask<T>`。
4. **子任务很多且属于父任务生命周期**：`TaskScope`。
5. **一批并发任务需要逐个收割结果**：`TaskGroup<T>`。
6. **阻塞代码不可避免**：`blocking()` / `spawnBlocking()` / `Thread`。
7. **协议读写**：优先 `BufStream<T>`。
8. **资源清理**：优先 `finally()`，或在 `TaskScope`/RAII 中统一回收。

### 5.2 代码风格建议

- 除非是 demo，不要滥用 `.value()`。
- 除非任务真的应脱离所有者，否则不要无脑丢弃 `spawn()` 返回值。
- 文件与 socket 的顺序流接口默认不保证并发安全；并发访问时显式加锁或改用偏移版 API。
- 需要退出路径时，把 `signal::ctrlC()`、stop token、scope cleanup 一起设计进去。

### 5.3 常见反模式

#### 反模式 1：在协程里直接阻塞

```cpp
std::this_thread::sleep_for(1s);
```

应改成：

```cpp
co_await ilias::sleep(1s);
```

#### 反模式 2：accept 后直接裸 `spawn()` 且没有回收策略

短 demo 可以，正式代码建议 `TaskScope` 或 `TaskGroup`。

#### 反模式 3：把 `read()` 当成“读满”

`read()` 只保证读到一些数据，不保证读满缓冲区；要读定长，使用 `readAll()`。

#### 反模式 4：在取消路径忘记清理

如果一个任务持有文件、socket、事务或外部资源，优先：

```cpp
co_await finally(mainTask(), cleanupTask());
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

## 6. 推荐实现模板

### 6.1 可取消的连接处理协程

```cpp
auto handleClient(ilias::TcpStream stream) -> ilias::Task<void> {
    auto token = co_await ilias::this_coro::stopToken();
    auto io = ilias::BufStream{std::move(stream)};

    while (!token.stop_requested()) {
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
```

### 6.2 结构化服务主循环

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
            scope.spawn(handleClient(std::move(stream)));
        }
    });
}
```

### 6.3 子进程 + 异步等待

```cpp
auto runCommand() -> ilias::IoTask<void> {
    auto out = co_await ilias::Process::Builder{"powershell"}
        .args({"-Command", "Get-ChildItem"})
        .output();
    if (!out) {
        co_return ilias::Err(out.error());
    }
    co_return {};
}
```

## 7. agent 在阅读源码时的优先级

如果 agent 需要继续深挖实现，建议按下面顺序读：

1. `include/ilias/platform.hpp`
2. `include/ilias/task.hpp`
3. `include/ilias/task/task.hpp`
4. `include/ilias/task/spawn.hpp`
5. `include/ilias/task/scope.hpp` / `group.hpp`
6. `include/ilias/io/stream.hpp` / `method.hpp`
7. `include/ilias/net/tcp.hpp` / `udp.hpp` / `addrinfo.hpp`
8. `include/ilias/fs/file.hpp`
9. `include/ilias/sync/*`
10. 平台后端：`platform/iocp.hpp`、`epoll.hpp`、`uring.hpp`、`qt.hpp`

### 不建议默认依赖的内容

除非你正在扩展运行时或适配新平台，否则尽量不要直接依赖：

- `include/ilias/detail/*`
- `include/ilias/runtime/*` 的内部实现细节
- 平台后端私有 awaiter 与内部描述符结构

## 8. 总结

Ilias 的公开接口其实很统一：

- 用执行器承载协程
- 用 `Task`/`IoTask` 表达异步结果
- 用 `TaskScope`/`TaskGroup` 管理并发生命周期
- 用 `BufStream`、`File`、`TcpStream`、`TlsStream` 组合具体 I/O
- 用 stop token 和 `finally()` 处理取消与清理

对 agent 来说，最重要的不是记住每个类的所有成员，而是坚持这套项目原生的异步风格：**非阻塞、可取消、结构化、统一流接口、显式生命周期管理。**
