# Ilias

> A lightweight asynchronous I/O library based on C++20 stackless coroutines, completion-based, and inspired by Tokio's design.

<!-- Project Info Badges -->
[![License](https://img.shields.io/github/license/BusyStudent/Ilias)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![Build System](https://img.shields.io/badge/build-xmake-green)](https://xmake.io)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)](https://github.com/BusyStudent/Ilias)

English | [中文](README_zh.md)

## Features

- Zero-dependency core library
- Cancellation support
- Structured concurrency support (using `TaskScope` and `TaskGroup`)
- Full networking support (TCP / UDP / async DNS resolution)
- File I/O
- TLS support (Windows: Schannel / Others: OpenSSL)
- Cross-platform (Windows / Linux)
- Single-threaded executor, easy to integrate with frameworks like Qt and convenient for development

## Table of Contents

- [Ilias](#ilias)
  - [Features](#features)
  - [Table of Contents](#table-of-contents)
  - [📊 CI Status](#-ci-status)
  - [Quick Start](#quick-start)
    - [Adding to Your Project](#adding-to-your-project)
      - [For xmake projects](#for-xmake-projects)
      - [For CMake projects](#for-cmake-projects)
    - [Basic Environment](#basic-environment)
      - [Simplified with the `ilias_main` macro](#simplified-with-the-ilias_main-macro)
    - [Network Programming](#network-programming)
      - [Simple Message Sending](#simple-message-sending)
      - [Accepting Connections](#accepting-connections)
    - [Spawning Coroutines](#spawning-coroutines)
    - [Error Handling](#error-handling)
      - [Two Ways to Handle Errors](#two-ways-to-handle-errors)
    - [Qt Integration](#qt-integration)
    - [Cancellation](#cancellation)
    - [Utilities](#utilities)
      - [whenAny](#whenany)
      - [whenAll](#whenall)
      - [setTimeout](#settimeout)
      - [unstoppable](#unstoppable)
      - [finally](#finally)
      - [this\_coro](#this_coro)
    - [Synchronization Primitives](#synchronization-primitives)
      - [Channel](#channel)
      - [Mutex](#mutex)
      - [TaskGroup](#taskgroup)
      - [TaskScope](#taskscope)
    - [Optional Dependencies](#optional-dependencies)
    - [Backends](#backends)
    - [System Requirements](#system-requirements)
      - [Compiler Support](#compiler-support)
      - [C++ Standard](#c-standard)
      - [Build System](#build-system)
    - [Known Limitations](#known-limitations)
  - [Contributing](#contributing)
  - [License](#license)

## 📊 CI Status

| CI Name | Status |
| --------- | ------- |
| Windows  | [![CI for windows by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml) |
| Linux    | [![CI for linux by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml) |
| Coverage | [![codecov](https://codecov.io/gh/BusyStudent/Ilias/graph/badge.svg?token=W9MQGIPX6F)](https://codecov.io/gh/BusyStudent/Ilias)|

## Quick Start

### Adding to Your Project

#### For xmake projects

```lua
add_repositories("btk-repo https://github.com/Btk-Project/xmake-repo.git")
add_requires("ilias")

target("your_app")
    add_packages("ilias")
```

#### For CMake projects

``` cmake
include(FetchContent)

FetchContent_Declare(
    ilias
    GIT_REPOSITORY https://github.com/your-username/ilias.git
)

FetchContent_MakeAvailable(ilias)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ilias::ilias)
```

### Basic Environment

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto main() -> int {
    ilias::PlatformContext ctxt; // Create an I/O context for submitting tasks
    ctxt.install(); // Register it to the current thread
    
    // Currently available I/O contexts: IocpContext, EpollContext, UringContext, QIoContext
    // A simple executor without I/O is: EventLoop
    // PlatformContext is a typedef, automatically selected based on the compilation platform
    
    auto task = []() -> ilias::Task<int> { // Coroutine function, return value must be Task<T>
        co_return 1;
    };
    
    auto result = task().wait(); // Create the task and block until completion
    // Task<T> represents a return value of T
    assert(result == 1);
    return 0;
}
```

#### Simplified with the `ilias_main` macro

If you want to simplify the code, you can use the `ilias_main` macro, which is equivalent to the code above.

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

void ilias_main() {
    co_return;
}

// Or return an int
int ilias_main() {
    co_return 0;
}

// Supports two parameter formats 
// - ilias_main()
// - ilias_main(int argc, char** argv)
// Return value supports void and int
// The `auto ->` syntax is currently not supported due to implementation limitations
```

### Network Programming

#### Simple Message Sending

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpStream;

void ilias_main() {
    auto client = (co_await TcpStream::connect("127.0.0.1:8080")).value();
    
    // ilias::makeBuffer converts any type convertible to std::span<T>
    // into std::span<const std::byte> (Buffer) or std::span<std::byte> (MutableBuffer)
    // The parameters for read and write are MutableBuffer and Buffer respectively
    // read and write return an IoTask<size_t>
    // IoTask<T> is an alias for Task<Result<T, std::error_code>>, indicating that an error might occur (see the Error Handling section for details)
    
    std::string_view sv = "HELLO WORLD";
    if (auto res = co_await client.write(ilias::makeBuffer(sv)); !res) {
        co_return;
    }
}
```

#### Accepting Connections

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

using ilias::TcpListener;
using ilias::TcpStream;
using ilias::IPEndpoint;

// Coroutine to handle a client connection
auto handleClient(TcpStream stream) -> ilias::Task<void> {
    std::array<std::byte, 1024> buffer;
    
    // Read data and echo it back
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
        // Spawn a new coroutine for each client
        auto handle = ilias::spawn(handleClient(std::move(stream)));
        // The handle can be used to check for completion or to wait for it
        // Discarding the handle is equivalent to detaching
    }
}
```

### Spawning Coroutines

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

void ilias_main() {
    // Spawn a coroutine
    auto handle = ilias::spawn(task());
    
    // Spawn a blocking task, which will be submitted to a thread pool
    auto handle2 = ilias::spawnBlocking(callable);
    
    // The handle can be used to check for completion or to wait for it
    co_await std::move(handle);

    // For finer control over the coroutine's lifecycle, use TaskScope or TaskGroup<T>
}
```

### Error Handling

Both error codes and exceptions are supported. The core type is `Result<T, E>`, an alias for `std::expected<T, E>`. Depending on the C++ version, it will use either the standard library implementation (C++23) or a third-party one (zeus_expected).

- Exceptions propagate up the call stack and are thrown at the `await` point.
- `Result<T, E>` is equivalent to `std::expected<T, E>`.
- `Err<T>` is equivalent to `std::unexpected<T>`.
- `IoResult<T>` is equivalent to `Result<T, std::error_code>`.

#### Two Ways to Handle Errors

```cpp
auto example() -> ilias::Task<void> {
    // Method 1: Use value() (throws an exception on error, catch it at the top level with try-catch)
    auto stream = (co_await TcpStream::connect("example.com:80")).value();
    
    // Method 2: Explicitly check for errors
    auto result = co_await TcpStream::connect("example.com:80");
    if (!result) {
        std::println("Connection failed: {}", result.error().message());
        co_return;
    }
    // Use *result
}
```

### Qt Integration

```cpp
#include <ilias/platform/qt.hpp>
#include <QApplication>

auto main(int argc, char **argv) -> int {
    QApplication app(argc, argv);
    ilias::QIoContext ctxt; // An I/O context integrated with Qt
    ctxt.install();
    
    // Subsequent code is the same as on other platforms; coroutines can be used normally
    
    return app.exec();
}
```

### Cancellation

Cancellation is supported. A cancellation request will stop the execution of the current coroutine at an `await` point, similar to throwing an uncatchable exception.

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

using namespace std::literals;

void ilias_main() {
    auto task = []() -> ilias::Task<int> {
        co_return 1;
    };
    auto handle = ilias::spawn(task());
    handle.stop(); // Send a cancellation signal

    // The result of `co_await` on a `WaitHandle<T>` is `Option<T>` (an alias for `std::optional`, with `void` automatically replaced by `std::monostate`)
    // If it's `nullopt`, the task was cancelled
    auto res = co_await std::move(handle);
    assert(res == 1); // Since this task has no await points, the cancellation will not succeed

    // A task with an await point
    auto task2 = []() -> ilias::Task<int> {
        co_await ilias::sleep(1000ms);
        co_return 1;
    };
    auto handle2 = ilias::spawn(task2());
    handle2.stop();
    auto res2 = co_await std::move(handle2);
    assert(res2 == std::nullopt); // Since sleep is an await point, cancellation will succeed
}
```

### Utilities

#### whenAny

Waits for any one of N awaitables to complete. Returns `std::tuple<Option<T1>, Option<T2>, ...>`. The others will be cancelled, and it will wait for their cancellation to complete.

```cpp
auto fn() -> ilias::Task<void> {
    auto [a, b] = co_await whenAny(taskA(), taskB());
    if (a) { // taskA() finished first
        // Use *a
    }
    if (b) { // taskB() finished first
        // Use *b
    }
}
```

#### whenAll

Waits for all N awaitables to complete. Returns `std::tuple<T1, T2, ...>`.

```cpp
auto fn() -> ilias::Task<void> {
    // Returns only after both have completed
    auto [a, b] = co_await whenAll(taskA(), taskB());
    // Use a and b
}
```

#### setTimeout

Cancels an awaitable after a specified duration. Returns `Option<T>`.

```cpp
auto fn() -> ilias::Task<void> {
    if (auto res = co_await setTimeout(doJob(), 1s); res) {
        // doJob completed within 1s
    } 
    else {
        // Timed out, doJob was cancelled
    }
}
```

#### unstoppable

Creates an unstoppable scope. Awaitables within it cannot be cancelled.

```cpp
auto fn = []() -> ilias::Task<void> {
    co_await unstoppable(sleep(1s));
};

auto example() -> ilias::Task<void> {
    auto handle = ilias::spawn(fn());
    handle.stop(); // Has no effect, sleep will not be cancelled
}

// Pipe syntax
auto example2() -> ilias::Task<void> {
    co_await (doJob() | unstoppable());
}
```

#### finally

Ensures a block of code is executed when the awaitable finishes (including on exception or cancellation).

```cpp
auto fn() -> ilias::Task<void> {
    int fd = co_await openFile();
    
    co_await finally(doJob(), [&]() -> ilias::Task<void> {
        // The cleanup handler can capture local variables
        // It's guaranteed that these variables are still alive during execution
        // You can `co_await` here to perform asynchronous cleanup
        co_await asyncCloseFile(fd);
        co_return;
    });
}
```

#### this_coro

This namespace contains many operations related to the current coroutine.

```cpp
auto fn() => ilias::Task<void> {
    // Get the cancellation token (std::stop_token) for the current coroutine
    auto token = co_await this_coro::stopToken();

    // Get the executor bound to the current coroutine
    auto &executor = co_await this_coro::executor();

    // Get the current stacktrace
    auto trace = co_await this_coro::stacktrace();
    std::println("We are on {}", trace);

    // Manually suspend the current coroutine to let the scheduler run others
    co_await this_coro::yield();

    // Check if the current coroutine has been requested to stop. Similar to getting the token and calling token.stop_requested()
    if (co_await this_coro::isStopRequested()) {

    }

    // Manually set the current coroutine to the stopped state
    // This only takes effect if stop_requested() is true
    co_await this_coro::stopped();
}
````

### Synchronization Primitives

Supports various synchronization tools: Channel, Mutex, TaskGroup.

#### Channel

Currently supports two types of channels: oneshot and mpsc.

```cpp
auto fn() -> ilias::Task<void> {
    // Create a channel
    // The argument 3 is the capacity. Sending will block if the capacity is exceeded.
    // A oneshot channel is equivalent to a channel with a capacity of 1.
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

    // Locking a Mutex returns a MutexGuard, which automatically releases the lock upon destruction.
    {
      auto guard = co_await mutex.lock();
      guard.unlock(); // Release early
    }

    // Sometimes, users need to manage the lock manually.
    {
        auto guard = co_await mutex.lock();
        guard.leak(); // Transfer the responsibility of releasing the lock to the user.
        mutex.unlockRaw(); // Manually release the lock.
    }
}
```

#### TaskGroup

```cpp
auto fn() -> ilias::Task<void> {
    // T is the return value type
    // If the group is destructed while tasks are still running, all tasks will receive a cancellation signal and be detached.
    auto group = ilias::TaskGroup<void> {};
    group.spawn(taskA());
    group.spawn(taskB());

    // Wait for all tasks to complete. Returns `std::vector<T>` (`void` is automatically converted to `std::monostate`).
    co_await group.waitAll();

    // Send a cancellation signal to all tasks, then wait for them to complete and discard the return values.
    co_await group.shutdown(); 

    // Wait for the next task to complete. Returns `Option<T>`.
    co_await group.next();
}
```

#### TaskScope

```cpp
auto fn() -> ilias::Task<void> {
    // Function version
    // Recommended for most cases as it's the safest and simplest.
    auto val = co_await TaskScope::enter([](auto &scope) -> ilias::Task<int> {
        scope.spawn(another()); // Other tasks can be spawned within the scope.
        co_return 42;
    });
    // Guarantees all tasks are completed upon leaving the scope.
    assert(val == 42);

    // Object version (for when you want to use a scope as a class member).
    TaskScope scope;
    scope.spawn(another());

    // Since C++ doesn't have async destructors, you must ensure the scope is empty before its destruction.
    // Otherwise, it will abort. It's recommended to place the wait in a `finally` block.
    co_await scope.waitAll();
}
```

### Optional Dependencies

- OpenSSL (for TLS support on non-Windows platforms)
- liburing (used by `UringContext`)

### Backends

| Backend | Platform | Status | Minimum Requirement |
|------|------|------|----------|
| epoll | Linux | Completed | Linux 4.3+ |
| IOCP | Windows | Completed | Windows 7+ |
| io_uring | Linux | Completed | Linux 5.1+ |
| Qt | Most should work | Completed | Qt 6+ |

### System Requirements

- Windows: 7+ (uses AFD)
- Linux: 4.3+ (at least epoll is required)

#### Compiler Support

- GCC: 11+
- Clang: 15+ (requires CTAD for alias)
- MSVC: (Visual Studio 2022)

#### C++ Standard

- C++20 or higher (C++23 is recommended)

#### Build System

- xmake (recommended)
- cmake

### Known Limitations

- Currently only supports Linux and Windows.
- macOS support is planned (but I don't have a macOS device).

## Contributing

Issues and Pull Requests are welcome!

## License

Licensed under the [MIT License](LICENSE).

Star ⭐ this project if you find it useful!
