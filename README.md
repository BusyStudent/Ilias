# Ilias

## A header-only network library based on C++20 coroutines

### Introduction

Ilias is a header-only network library built on C++20 coroutines. It's designed for ease of use and efficiency, with minimal dependencies. You only pay for what you use.

### CI Status

| CI Name | Status |
| --------- | ------- |
| Windows  | [![CI for windows by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml) |
| Linux    | [![CI for linux by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml) |
| Coverage | [![codecov](https://codecov.io/gh/BusyStudent/Ilias/graph/badge.svg?token=W9MQGIPX6F)](https://codecov.io/gh/BusyStudent/Ilias)|

### Features

- Lightweight
- Easy to use
- Support for multiple backends
- Cancellation support
- Exception support (error codes used by default)
- HTTP/1.1 with SSL support (optional)

### Examples

#### HTTP Client

```cpp
#include <ilias/platform.hpp>
#include <ilias/http.hpp>
using namespace ILIAS_NAMESPACE;

int main() {
    PlatformContext ctxt; // Default Platform I/O Context (IOCP on Windows, epoll on Linux)
    HttpSession session(ctxt);

    // Result<T> is an alias for std::expect<T, Error>
    // Task<T> return value is Result<T>
    // If target std is not greater than C++23, we use a built-in implementation instead

    auto task = [&]() -> Task<> {
        auto reply = co_await session.get("https://www.google.com");
        if (!reply) {
            co_return Unexpected(reply.error());
        }
        auto text = co_await reply->text();
        if (!text) {
            co_return Unexpected(text.error());
        }
        std::cout << text.value() << std::endl;
        co_return {};
    };
    task().wait(); // Blocking wait for the task to complete
}
```

#### Socket Operations

```cpp
// Setup similar to HTTP client example
auto listener = [&]() -> Task<> {
    TcpListener listener(ctxt, AF_INET);
    while (auto value = co_await listener.accept()) {
        auto &[client, addr] = *value;
        // Handle connection...
        spawn(handleIt, std::move(client), std::move(addr));
    }
    co_return {};
};

auto client = [&]() -> Task<> {
    TcpClient client(ctxt, AF_INET);
    if (auto ok = co_await client.connect("127.0.0.1:8080"); !ok) {
        std::cout << "Connection failed: " << ok.error().toString() << std::endl;
        co_return Unexpected(ok.error());
    }
    auto bytes = co_await client.readAll(makeBuffer(buffer, len));
    // Process data...
    co_return {};
};

auto clientWithExceptions = [&]() -> Task<> {
    TcpClient client(ctxt, AF_INET);
    try {
        (co_await client.connect("127.0.0.1:8080")).value();
        (co_await client.readAll(makeBuffer(buffer, len))).value();
        // Process data...
    }
    catch (const BadExpectedAccess<Error> &e) {
        std::cout << "Network operation failed: " << e.error().toString() << std::endl;
    }
    co_return {};
};

// It's okay not to catch the exception; the task's value will be the error stored in the exception
auto val = co_await clientWithExceptions();
val.error(); // Access the error if the operation failed
```

#### Error Handling

```cpp
auto task = []() -> Task<> {
    auto val = co_await xxx();
    if (!val) {
        // Handle error
    }
    // Alternatively:
    auto val = (co_await xxx()).value(); // Throws BadExpectedAccess<Error> if no value
    // Process val...
};

auto v = co_await task(); // If BadExpectedAccess<Error> is thrown in the task,
                          // the task's value will be the error object stored in the exception
```

### Backends

| Backend | Progress | Description |
| --------- | ---------- | ------------- |
| epoll     | Completed | Linux only |
| IOCP      | Completed | Windows only |
| Qt        | Completed | Poll model, using QSocketNotifier |
| io_uring  | Completed | Linux only |
