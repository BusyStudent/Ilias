# Ilias

## A header-only network library based on cpp20 corotinue

### 0 introduction

Ilias is a header-only network library based on cpp20 coroutine. It is designed to be easy to use and efficient.

### 1 features

- light weight
- easy to use
- efficient
- support for multiple backends
- support for cancel
- support exceptions, but we use error code default

### 2 examples

- http client

``` cpp
#include <ilias_networking.hpp>
#include <ilias_http.hpp>
using namespace ILIAS_NAMESPACE;
int main() {
    PlatformIoContext ctxt; //< Default select io context or you can select another one
    HttpSession session(ctxt);

    //< Result<T> is an alias for std::expect<T, Error>
    //< Task<T> return value is Result<T>
    //< If target std is not greater than 23, we will use bultin impl instead

    auto task = [&]() -> Task<> {
        auto reply = co_await ctxt.get("https://www.google.com");
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

- socket

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

### 3 backends

| backend | progress | description |
| --------- | ---------- | ------------- |
| epoll     | done         | linux only |
| iocp      | almost done| windows only, not good support for cancel |
| qt        | done | poll model, using QSocketNotifier |
