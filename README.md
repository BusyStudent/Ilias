# Ilias

## A header-only async coroutine IO library for C++, based on C++20 stackless coroutines

### Introduction

A lightweight async coroutine library aimed at minimal dependencies, implemented using C++20 stackless coroutines

- Core has no external dependencies
- Built-in facilities for cancellation operations
- Structured concurrency
- Network support (TCP, UDP, UnixSocket, Async-GetAddrinfo)
- File I/O and pipe support
- SSL support (using Schannel on Windows, OpenSSL on other platforms)
- Built-in small HTTP 1.1 client and WebSocket client support
- Cross-platform (Windows, Linux)
- Simple single-threaded scheduler, easy to integrate with other frameworks like Qt

### [English](README.md) | [中文](README_zh.md)

### CI Status

| CI Name | Status |
| --------- | ------- |
| Windows  | [![CI for windows by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml) |
| Linux    | [![CI for linux by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml) |
| Coverage | [![codecov](https://codecov.io/gh/BusyStudent/Ilias/graph/badge.svg?token=W9MQGIPX6F)](https://codecov.io/gh/BusyStudent/Ilias)|

## Quick Start

### Adding to Your Project

## For xmake projects

```lua
add_repositories("btk-repo https://github.com/Btk-Project/xmake-repo.git")
add_requires("ilias")
```

## Or use git submodule

```bash
git submodule add https://github.com/BusyStudent/Ilias.git
```

## For a simpler approach

Just copy all files from the include directory into your project

### Basic Environment

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt; // First, build a context for submitting tasks - this is thread_local, one per thread
    // Available IO contexts include IocpContext, EpollContext, UringContext, QIoContext
    // Simple executors without IO include MiniExecutor
    // PlatformContext is a typedef that selects based on the current platform at compile time
    auto task = []() -> ilias::Task<int> { // This is a coroutine, return type must be Task<T>
        co_return 1;
    };
    auto result = task().wait(); // Create task and block waiting for completion
    // Task<T> represents return value of type T, so T can be used below
    assert(result == 1);
    return 0;
}
```

For convenience, there's an ilias_main macro equivalent to the above:

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

void ilias_main() {
    co_return;
}
// or
int ilias_main() {
    co_return 0;
}
// () supports both () and (int argc, char** argv) formats
// Return value supports void and int
```

### Socket

Simple message sending:

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpClient;
using ilias::IPEndpoint;

void ilias_main() { // Taking a shortcut :)
    auto endpoint = IPEndpoint::fromString("127.0.0.1:8080").value();
    auto client = (co_await TcpClient::make(endpoint.family)).value();
    if (auto res = co_await client.connect(endpoint); !res) {
        co_return;
    }
    // ilias::makeBuffer converts anything that can be converted to std::span<T> into std::span<const std::byte> or std::span<std::byte>
    // read and write parameters are std::span<const std::byte> and std::span<std::byte> respectively
    // read and write return an IoTask<size_t>
    // IoTask<T, E = Error> is an alias for Task<Result<T, E>>, representing possible errors - see error handling section
    std::string_view sv = "HELLO WORLD";
    if (auto res = co_await client.write(ilias::makeBuffer(sv)); !res) {
        co_return;
    }

    // Alternative way to construct TcpClient
    auto ctxt = co_await ilias::currentIoContext();
    TcpClient client2(ctxt, AF_INET);
}
```

Waiting for connections:

```cpp
#include <ilias/sync/scope.hpp>
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpListener;
using ilias::IPEndpoint;
using ilias::TaskScope;

void ilias_main() { // Taking shortcuts, not handling errors for demo, using value() directly :)
    auto endpoint = IPEndpoint::fromString("127.0.0.1:8080").value();
    auto scope = co_await TaskScope::make(); // TaskScope ensures all child coroutines complete when exiting
    auto listener = (co_await TcpListener::make(endpoint.family)).value();
    while (true) {
        auto [client, _] = (co_await listener.accept()).value();
        auto handle = scope.spawn(handleClient, std::move(client)); // Create child task in scope. Could use ilias::spawn directly but less safe
        // handle can check completion or wait for completion. Losing handle is like detach
    }
}
```

### HTTP Client

Simple HTTP requests supporting GET, POST, HEAD etc:

```cpp
#include <ilias/platform.hpp>
#include <ilias/http.hpp>
#include <ilias/task.hpp>

void ilias_main() { // Taking a shortcut :)
    auto session = co_await ilias::HttpSession::make();
    auto reply = (co_await session.get("https://www.google.com")).value();
    auto content = (co_await reply.text()).value();
    std::cout << "Http to " << reply.url().toString() << " Status code" << reply.statusCode() << std::endl;
    std::cout << content << std::endl;
}
```

### Error Handling

Supports error codes and exceptions, using ```Result<T, E = Error>``` which is an alias for ```Expected<T, E>```.
```Expected<T>``` uses either the standard library implementation (C++23) or built-in implementation based on availability.

```Error``` is equivalent to ```std::error_code```. You can define your own error codes by implementing ```ErrorCategory``` and binding with ```ILIAS_DECLARE_ERROR(errc, category)```, otherwise manually construct ```Error``` objects like ```Error(yourCode, categoryReference)```.

```cpp
auto fn() -> IoTask<void> {
    auto ret = co_await someTask();
    ret.value(); // Throws BadExpectedAccess<Error> if no value. IoTask<T> auto-converts BadExpectedAccess<Error> to Error in return value
    // Manual checking:
    if (ret) {
        ret.value();
    }
    else {
        co_return Unexpected(ret.error());
    }
    // Or use try-catch
    try {
        ret.value();
    }
    catch (const BadExpectedAccess<Error> &e) {
        auto err = e.error();
    }
    // Or let errors propagate up (note: error types must match, e.g., IoTask<void, Error1> and IoTask<void, Error2> can't use this)
    ret.value();
    // Or use safe syntactic sugar that checks errors and unwraps Result like Rust's ? (zero-cost with Statement Expression support, otherwise uses exceptions)
    auto val = ilias_try(ret);
    auto val2 = ilias_try(co_await someTask());

    // Regular exceptions or mismatched error types propagate up, not caught by IoTask<T>
    throw 1; // Exception rethrown at co_await fn(); or fn().wait()
}
```

### Qt Integration

```cpp
#include <ilias/platform/qt.hpp>
#include <ilias/http.hpp>
#include <QApplication>

auto main(int argc, char **argv) -> int {
    QApplication app(argc, argv);
    ilias::QIoContext ctxt; // Qt-integrated IoContext
    // Code same as above - ready to use
}
```

### Synchronization

Supports multiple synchronization methods: Channel, Mutex, whenAny, whenAll

- whenAny:

```cpp
auto fn() -> Task<void> {
    // Wait for either task to complete, other task gets cancellation notice and result discarded
    auto [a, b] = co_await whenAny(taskA(), taskB());
    if (a) { // taskA() completed first

    }
    if (b) { // taskB() completed first

    }
    // Vector type
    std::vector<Task<void>> tasks;
    tasks.emplace_back(taskA());
    tasks.emplace_back(taskB());
    for (auto &result : co_await whenAny(tasks)) {
        
    }
}
```

- whenAll:

```cpp
auto fn() -> Task<void> {
    // Returns only when both complete
    auto [a, b] = co_await whenAll(taskA(), taskB());
    // use a & b

    // Vector type
    std::vector<Task<void>> tasks;
    tasks.emplace_back(taskA());
    tasks.emplace_back(taskB());
    auto res = co_await whenAll(tasks); // Single return value
}
```

- Channel:

```cpp
auto fn() -> Task<void> {
    // Create channel
    auto [sender, receiver] = mpmc::channel<int>(3); // 3 is capacity, blocks if send would exceed capacity. Default size_t::max() means unlimited
    co_await sender.send(1);
    auto val = co_await receiver.recv();
}
```

### Dependencies

- zlib (optional)
- openssl (optional)
- liburing (optional, used by UringContext)

### Backends

| Backend | Progress | Description |
| --------- | ---------- | ------------- |
| epoll     | Completed |         |
| IOCP      | Completed |         |
| Qt        | Completed | Qt Integration |
| io_uring  | Completed |         |
