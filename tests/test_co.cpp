#define ILIAS_ENABLE_GO
#include "../include/ilias_co.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>
#include <string>

using namespace ILIAS_NAMESPACE;

auto coSleep(int s) {
    return CallbackAwaitable<>(
        [s](CallbackAwaitable<>::ResumeFunc &&resumeFunc) {
            std::cout << "sleeping... for " << s << std::endl;
            std::thread([resumeFunc, s]() {
                std::this_thread::sleep_for(std::chrono::seconds(s));
                std::cout << "sleeping done. " << std::endl;
                resumeFunc();
            }).detach();
        }
    );
}

auto a() -> Task<> {
    co_await coSleep(1);
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
    catch (int e) {
        std::cout << "catch " << e << "for co_await g()" << std::endl;
    }
    co_return 114514;
}
auto coToString(int num) -> Task<std::string> {
    co_return std::to_string(num);
}
auto anotherTask() -> Task<> {
    std::cout << "Another task" << std::endl;
    co_return;
}

int main() {
    std::ios::sync_with_stdio(true);
    MiniEventLoop loop;

    return loop.runTask([&]() -> Task<int> {
        std::cout << "Part 1" << std::endl;
        ilias_spawn anotherTask();

        auto self = co_await GetPromiseAwaitable();
        auto val = co_await testAwait();
        auto str = co_await coToString(val);
        std::cout << "testAwait return :" <<  val << std::endl;
        std::cout << "coToString return :" << str << std::endl;
        std::cout << "Part 2" << std::endl;
        co_return 0;
    }());
}