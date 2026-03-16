---
outline: deep
---

# API 总览

ilias 被划分成了以下模块

- platform 平台相关上下文
- runtime 运行时内部的接口
- fiber 有栈协程
- task 无栈协程
- sync 线程安全的同步
- net 网络
- tls tls封装
- io io公共抽象
- fs 文件系统

如果想直接使用整个模块的内容 使用 `#include <ilias/module_name.hpp>`

## 任务模型

模型使用stdexec的模型，任务执行后有三种结果

- 完成 有结果 (正常co_return)
- 完成 异常 (内部throw了)
- 停止 (收到了取消 而且底层响应了取消)

所以返回值分别为

```cpp
auto Task<T>::wait() -> T;
auto WaitHandle<T>::wait() -> Option<T>;
```

Task的wait是就地堵塞等待 无法取消 因此返回值是 `T` (有结果) 或者抛出异常 (异常)
WaitHandle 是spawn出去的任务 可以取消 因此返回值是 `Option<T>` (有结果) 或者抛出异常 (异常) 或者 `nullopt` (停止)

## 取消模型

取消在内部使用了 runtime::StopSource 和 runtime::StopToken 两个类, 目前是标准库的using

```cpp
namespace runtime {
    using StopSource = std::stop_source;
    using StopToken = std::stop_token;
}
```

取消只有在await点才会发生 而且awaiter 需要响应来自 StopSource 的取消信号

``` cpp
auto fn() -> Task<void> {
    // do cpu compute
    co_return;
}

auto handle = spawn(task());
handle.stop(); // 取消并不会生效 因为没人响应
EXPECT_TRUE(co_await std::move(fn)); // 有结果

```

``` cpp
auto fn() -> Task<void> {
    co_await sleep(10s);
}

auto handle = spawn(task());
handle.stop(); // 取消会生效 因为有sleep响应了取消信号
EXPECT_FALSE(co_await std::move(fn)); // 停止
```

### 任务调度

任务调度使用 runtime::Scheduler, 目前是标准库的using

```cpp
namespace runtime {
    using Scheduler = std::execution::scheduler;
}
```

## 具体细节

TODO: 待补充
