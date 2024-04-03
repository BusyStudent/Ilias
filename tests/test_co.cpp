#define ILIAS_COROUTINE_TRACE
#include "../include/ilias_co.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>
#include <string>

using namespace ILIAS_NAMESPACE;

// Test AwaitTransform
template <>
struct ILIAS_NAMESPACE::AwaitTransform<std::string> {
    std::suspend_never transform(const std::string &s) {
        return {};
    }
};

static_assert(AwaitTransformable<Task<int> >);
static_assert(AwaitTransformable<Task<std::string> >);

auto a() -> Task<> {
    // throw std::runtime_error("Error");
    co_await msleep(1000);
    co_return Result<>();
}
auto b() -> Task<int> {
    co_await a();
    co_return 1;
}
auto c() -> Task<> {
    // Test erase awaitable detailed type
    co_await b();
    co_return Result<>();
}

auto d() -> Task<> {
    co_return co_await c();
}

auto e() -> Task<> {
    co_return co_await d();
}

auto f() -> Task<> {
    co_return co_await e();
}

auto g() -> Task<> {
    co_return co_await f();
}
auto testAwait() -> Task<int> {
    std::cout << "testAwait" << std::endl;
    try {
        co_await g();
    }
    catch (std::exception &exception) {
        std::cout << "catch " << exception.what() << " for co_await g()" << std::endl;
    }
    co_return 114514;
}
auto coToString(int num) -> Task<std::string> {
    co_return std::to_string(num);
}
auto anotherTask() -> Task<> {
    if (auto ret = co_await msleep(2000); !ret) {
        std::cout << ret.error().message() << std::endl;
        co_return Result<>();
    }
    std::cout << "Another task" << std::endl;
    co_return Result<void>();
}

int main() {
    std::ios::sync_with_stdio(true);
    MiniEventLoop loop;

    return loop.runTask([&]() -> Task<int> {
        std::cout << "Part 1" << std::endl;
        auto handle = loop.spawn(anotherTask);

        co_await msleep(1000);
        handle.abort();

        auto val = co_await testAwait();
        auto str = co_await coToString(val.value());
        std::cout << "testAwait return :" <<  val.value() << std::endl;
        std::cout << "coToString return :" << str.value() << std::endl;
        std::cout << "Part 2" << std::endl;
        co_await msleep(3000);
        std::cout << "Part 3" << std::endl;
        co_return 0;
    }()).value_or(-1);
}