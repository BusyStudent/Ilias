#define ILIAS_COROUTINE_TRACE
#include "../include/ilias_task.hpp"
#include "../include/ilias_await.hpp"
#include "../include/ilias_channel.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>
#include <string>

using namespace ILIAS_NAMESPACE;
using namespace std::chrono_literals;

Task<int> another() {
    co_return 42;
}
Task<int> taskAA() {
    ::printf("go\n");
    co_return 11;
}

Task<int> task() {
    // co_await 
    ilias_go taskAA();
    auto result = co_await WhenAny(Sleep(20ms), Sleep(1s));

    ilias_select {
        taskAA() >> [&](auto v) {
            ::printf("Select >> taskAA() => %d\n", v.value());
        },
        Sleep(1s) >> [&](auto v) {
            ::printf("Select >> Sleep(1s)\n");
        },
        Sleep(10s) >> nullptr
    };
    co_return 0;
}


int main() {
    MiniEventLoop loop;
    ilias_wait Sleep(1s);
    auto ret = ilias_wait task();
    return ret.value_or(-1);
}