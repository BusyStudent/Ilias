#define ILIAS_ENABLE_GO
#include "../include/ilias_co.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>
#include <string>

using namespace ILIAS_NAMESPACE;

auto a() -> Task<> {
    throw std::runtime_error("Error");
    co_await msleep(1000);
    co_return;
}
auto b() -> Task<> {
    co_return co_await a();
}
auto c() -> Task<> {
    co_return co_await b();
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
    NativeEventLoop loop;

    return loop.runTask([&]() -> Task<int> {
        std::cout << "Part 1" << std::endl;
        co_yield anotherTask();

        auto val = co_await testAwait();
        auto str = co_await coToString(val);
        std::cout << "testAwait return :" <<  val << std::endl;
        std::cout << "coToString return :" << str << std::endl;
        std::cout << "Part 2" << std::endl;
        co_await msleep(3000);
        co_return 0;
    }());
}