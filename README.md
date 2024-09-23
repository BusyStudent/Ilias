# Ilias

## A header-only network library based on cpp20 corotinue

### Introduction

Ilias is a header-only network library based on cpp20 coroutine. It is designed to be easy to use and efficient.
with minimal dependencies, only pay for what you need.

### CI Status

| CI Name | Status |
| --------- | ------- |
| Windows  | [![CI for windows by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-windows.yml) |
| Linux    | [![CI for linux by xmake](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml/badge.svg)](https://github.com/BusyStudent/Ilias/actions/workflows/xmake-test-on-linux.yml) |
| Coverage | [![codecov](https://codecov.io/gh/BusyStudent/Ilias/graph/badge.svg?token=W9MQGIPX6F)](https://codecov.io/gh/BusyStudent/Ilias)|

### Features

- Light weight
- Easy to use
- Support for multiple backends
- Support for cancel
- Support exceptions, but we use error code default
- HTTP1 with SSL (optional)

### Examples

- Http client

``` cpp
#include <ilias/platform.hpp>
#include <ilias/http.hpp>
using namespace ILIAS_NAMESPACE;
int main() {
    PlatformContext ctxt; //< Default Platform Io Context (iocp on windows, epoll on linux), Or you can use your own
    HttpSession session(ctxt);

    //< Result<T> is an alias for std::expect<T, Error>
    //< Task<T> return value is Result<T>
    //< If target std is not greater than 23, we will use bultin impl instead

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
    task().wait(); //< Blocking wait the task done
}
```

- Socket

``` cpp
// Another things as same as http client
auto listener = [&]() -> Task<> {
    TcpListener listener(ctxt, AF_INET);
    while (auto value = co_await listener.accept()) {
        auto &[client, addr] = *value;
        // Handle it...
        spawn(handleIt, std::move(client), std::move(addr));
    }
    co_return {};
};
auto client = [&]() -> Task<> {
    TcpClient client(ctxt, AF_INET);
    if (auto ok = co_await client.connect("127.0.0.1:8080"); !ok) {
        std::cout << "Connect failed by " << ok.error().toString() << std::endl;
        co_return Unexpected(ok.error());
    }
    auto bytes = co_await client.readAll(makeBuffer(buffer, len));
    // ...
    co_return {};
};

auto clientWithExcpetions = [&]() -> Task<> {
    TcpClient client(ctxt, AF_INET);
    try {
        (co_await client.connect("127.0.0.1:8080")).value();
        (co_await client.readAll(makeBuffer(buffer, len))).value();
        // ...
    }
    catch (const BadExpectedAccess<Error> &e) {
        std::cout << "Network failed by " << e.error().toString() << std::endl;
    }
    co_return {};
};
// It is ok to not catch the expection, the task 's value will be the error stored in exception
auto val = co_await clientWithExceptions();
val.error(); //< 

```

- Error Handling

``` cpp

auto task = []() -> Task<> {
    auto val = co_await xxx();
    if (!val) {
        // Handle err?
    }
    // or
    auto val = (co_await xxx()).value(); //< If not has value, it will throw BadExpectedAccess<Error>
    // ...
}

auto v = co_await task(); //< If user throw BadExpectedAccess<Error> in task
// the value of the task will be the error object stored in exception

```

### Backends

| Backend | Progress | Description |
| --------- | ---------- | ------------- |
| epoll     | done | linux only |
| iocp      | done | windows only |
| qt        | done | poll model, using QSocketNotifier |
