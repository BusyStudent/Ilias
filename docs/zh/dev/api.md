---
outline: deep
---

# API 概览

Ilias 的公开接口可以按“运行时、任务、I/O、网络、同步、扩展能力”六层来理解。实际写代码时，不需要一次掌握所有头文件；只要按模块选对入口即可。

## 1. 推荐入口

- 总入口：`#include <ilias.hpp>`
- 运行时入口：`#include <ilias/platform.hpp>`
- 协程任务：`#include <ilias/task.hpp>`
- 通用流：`#include <ilias/io.hpp>`
- 网络：`#include <ilias/net.hpp>`
- 文件：`#include <ilias/fs.hpp>`
- 同步：`#include <ilias/sync.hpp>`
- TLS：`#include <ilias/tls.hpp>`
- 进程：`#include <ilias/process.hpp>`
- 信号：`#include <ilias/signal.hpp>`

## 2. 模块速查

| 模块 | 主要类型 / 函数 | 用途 |
| --- | --- | --- |
| platform | `PlatformContext`、`ilias_main` | 安装当前线程执行器，启动异步程序 |
| task | `Task<T>`、`spawn`、`whenAll`、`whenAny`、`TaskScope`、`TaskGroup<T>`、`Thread` | 编写与组织协程 |
| runtime | `Executor`、stop token、`this_coro::*` | 执行器、取消、协程上下文访问 |
| io | `BufReader`、`BufWriter`、`BufStream`、`readAll`、`writeAll`、`getline` | 统一的流式 I/O 编程 |
| net | `TcpStream`、`TcpListener`、`UdpSocket`、`AddressInfo` | 网络通信与地址解析 |
| fs | `File`、`OpenOptions` | 文件读写 |
| sync | `Mutex`、`Event`、`Semaphore`、`oneshot::channel<T>`、`mpsc::channel<T>()` | 协程间同步与消息传递 |
| tls | `TlsContext`、`TlsStream<T>` | TLS 客户端 / 服务端 |
| process | `Process::Builder`、`Process` | 异步子进程 |
| signal | `signal::ctrlC()` | 控制台程序优雅退出 |

## 3. 最重要的公共概念

### 3.1 `Task<T>` 与 `IoTask<T>`

- `Task<T>`：普通协程结果。
- `IoTask<T>`：I/O 协程结果，本质上是 `Task<Result<T, std::error_code>>`。

### 3.2 统一流接口

很多对象都遵循统一风格：

- `read(MutableBuffer)`
- `write(Buffer)`
- `flush()`
- `shutdown()`
- `close()` / `cancel()`

所以协议代码通常可以在 `TcpStream`、`TlsStream<T>`、`File`、`BufStream<T>` 之间复用。

### 3.3 结构化并发

- `TaskScope`：强调父子生命周期
- `TaskGroup<T>`：强调成批任务的结果收集

### 3.4 取消模型

Ilias 使用 stop token 模型，而不是把取消完全建立在异常之上。常见接口：

- `this_coro::stopToken()`
- `this_coro::isStopRequested()`
- `this_coro::stopped()`
- `handle.stop()`

## 4. 从哪里继续读

如果你是第一次接触项目，推荐阅读顺序：

1. [快速开始](/zh/guides/quick-start)
2. [使用 Ilias 构建异步软件](/zh/guides/build-async-software)
3. [Agent 参考手册](/zh/dev/agent-reference)

如果你要直接写代码，最常用的头文件通常只有：

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/io.hpp>
#include <ilias/net.hpp>
```

## 5. API 使用建议

- demo 与顶层入口可以使用 `.value()` 快速拆包。
- 正式库代码更推荐显式检查 `Result`。
- 非必要不要裸 `spawn()` 一堆子任务，优先 `TaskScope`。
- 协程里不要做同步阻塞，优先 `sleep()`、`blocking()`、`spawnBlocking()`。
- 文本协议优先 `BufStream<T>`，定长协议优先 `readAll()`。

## 6. 相关文档

- [快速开始](/zh/guides/quick-start)
- [什么是 ilias](/zh/guides/what-is-ilias)
- [使用 Ilias 构建异步软件](/zh/guides/build-async-software)
- [Agent 参考手册](/zh/dev/agent-reference)
