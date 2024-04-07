#define ILIAS_COROUTINE_TRACE
#include "../include/ilias_task.hpp"
#include "../include/ilias_await.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>
#include <string>

using namespace ILIAS_NAMESPACE;
using namespace std::chrono_literals;

Task<int> another() {
    co_await Sleep(10s);
    co_return 42;
}

Task<int> task() {
    co_await another();
    co_return 0;
}

int main() {
    MiniEventLoop loop;
    return task().get().value_or(-1);
}