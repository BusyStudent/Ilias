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
#include <ilias/networking.hpp>
#include <ilias/http.hpp>
using namespace ILIAS_NAMESPACE;
int main() {
    PlatformIoContext ctxt; //< Default select io context or you can select another one
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
        co_return Result<>();
    };
    ctxt.runTask(task()); //< Blocking wait the task done
}
```

- Socket

``` cpp
// Another things as same as http client
auto task = [&]() -> Task<> {
    TcpListener listener(ctxt, AF_INET);
    while (auto value = co_await listener.accept()) {
        auto &[client, addr] = *value;
        // Handle it...
        ctxt.spawn(handleIt, std::move(client), std::move(addr));
    }
    co_return Result<>();
};
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
| epoll     | done         | linux only |
| iocp      | almost done| windows only, not good support for cancel |
| qt        | done | poll model, using QSocketNotifier |
