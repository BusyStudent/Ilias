---
outline: deep
---

# Quick Start

## Add Dependency

First, choose your build system. ilias supports both xmake and cmake.

::: tip
Xmake is the primary build system, maintained more frequently, and is recommended for priority use.
:::

### Using xmake

```lua
add_repositories("btk-repo https://github.com/Btk-Project/xmake-repo.git")
add_requires("ilias")

target("your_app")
    add_packages("ilias")
```

### Using cmake

```cmake
include(FetchContent)

FetchContent_Declare(
    ilias
    GIT_REPOSITORY https://github.com/BusyStudent/Ilias.git
    GIT_TAG main
)

FetchContent_MakeAvailable(ilias)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ilias::ilias)
```

## Write Your First Program

Let's start with a very simple example.

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>

auto task() -> ilias::Task<int> {
    co_return 0;
}

auto main(int argc, char **argv) -> int {
    ilias::PlatformContext ctxt {}; // Create an executor for scheduling
    ctxt.install(); // Then install it into thread_local storage

    return task().wait(); // Create the task and block until it completes
}
```

This uses two major modules:

- `task`: Provides the main stackless coroutine APIs, including `Task`, `Generator`, `TaskGroup`, `spawn`, etc.
- `platform`: Provides platform-specific executor APIs, including `IocpContext`, `EpollContext`, `UringContext`. `PlatformContext` is an alias.

`platform` also provides a helper macro to simplify writing the `main` function.

```cpp
void ilias_main() {
    co_return;
}
```

## Write an Asynchronous Network Program

The program above doesn't do anything and seems a bit boring. Let's add something to it.

```cpp
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>

// Note: All functions that can fail return Result<T, E> (std::expected<T, E>)
// In this example, we lazily unwrap it directly using .value()
using namespace std::literals;

auto handle(ilias::TcpStream stream) -> ilias::Task<void> {
    auto content = ilias::makeBuffer("hello world\n"sv);

    (co_await stream.writeAll(content)).value();
    (co_await stream.flush()).value();
}
void ilias_main() {
    // First, create a listener
    auto listener = (co_await ilias::TcpListener::bind("127.0.0.1:8080")).value();
    while (true) {
        auto [stream, endpoint] = (co_await listener.accept()).value();

        // Create a child task to handle the connection. For better management, you can use TaskScope.
        ilias::spawn(handle(std::move(stream)));
    }
}
```

We find that letting the program run in an infinite loop like this is a bit annoying. Let's add an exit signal to it.

```cpp
#include <ilias/platform.hpp>
#include <ilias/signal.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <print>

// Same as the code above
auto handle(ilias::TcpStream stream) -> ilias::Task<void>;
auto accept(ilias::IPEndpoint endpoint) -> ilias::Task<void>;

void ilias_main() {
    auto [_, ctrlc] = co_await ilias::whenAny(
        accept("127.0.0.1:8080"),
        ilias::signal::ctrlC(),
    );
    if (ctrlc) {
        std::println("Server is shutting down...");
    }
}
```
