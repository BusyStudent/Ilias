// #define ILIAS_COROUTINE_TRACE
#define ILIAS_COROUTINE_LIFETIME_CHECK
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

Task<int> v() {
    co_return 123;
}

Task<int> task() {
    co_await v();
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

Task<void> printUtilNone(Receiver<int> r) {
    while (auto num = co_await r.recv()) {
        ::printf("%d\n", num.value());
    }
    r.close();
    co_return Result<void>();
}
Task<void> printUtilN(Receiver<int> r, int n) {
    while (auto num = co_await r.recv()) {
        ::printf("%d\n", num.value());
        n -= 1;
        if (n == 0) {
            break;
        }
    }
    r.close();
    co_return Result<void>();
}
Task<void> sendForN(Sender<int> s, int n) {
    for (int i = 0; i < n; ++i) {
        if (auto v = co_await s.send(i); !v) {
            ::printf("sendForN: send failed =>%s\n", v.error().message().c_str());
            break;
        }
    }
    s.close();
    co_return Result<void>();
}

Task<void> testChannel() {
    ::printf("testChannel1\n");
    auto [tx, rx] = Channel<int>::make();

    co_await WhenAll(sendForN(std::move(tx), 10), printUtilNone(std::move(rx)));
    co_return Result<void>();
}
Task<void> testChannel2() {
    ::printf("testChannel2\n");
    auto [tx, rx] = Channel<int>::make();
    
    co_await WhenAll(sendForN(std::move(tx), 100), printUtilN(std::move(rx), 3));
    co_return Result<void>();
}
Task<void> testWhenAll1() {
    co_await WhenAll(Sleep(1s), Sleep(10ms));
    co_return Result<void>();
}

int main() {
    MiniEventLoop loop;
    // ilias_wait Sleep(1s);
    auto ret = ilias_wait task();
    ilias_wait testChannel();
    ilias_wait testChannel2();
    ilias_wait testWhenAll1();
    return ret.value_or(-1);
}