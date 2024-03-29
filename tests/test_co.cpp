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
    co_await std::string("a");
    co_return;
}
auto b() -> Task<int> {
    co_await a();
    co_return 1;
}
auto c() -> Task<> {
    // Test erase awaitable detailed type
    IAwaitable awaitable = b();
    co_await awaitable;
    co_return;
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
    co_await msleep(2000);
    std::cout << "Another task" << std::endl;
    co_return;
}

int main() {
    std::ios::sync_with_stdio(true);
    MiniEventLoop loop;

    return loop.runTask([&]() -> Task<int> {
        std::cout << "Part 1" << std::endl;
        co_yield anotherTask();

        auto val = co_await testAwait();
        auto str = co_await coToString(val);
        std::cout << "testAwait return :" <<  val << std::endl;
        std::cout << "coToString return :" << str << std::endl;
        std::cout << "Part 2" << std::endl;
        co_await msleep(3000);
        std::cout << "Part 3" << std::endl;
        co_return 0;
    }());
}