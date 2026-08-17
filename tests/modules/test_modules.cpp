// Public macro API
#include <ilias/macros.hpp>

import ilias;
import std;

using namespace ilias;
using namespace std::chrono_literals;

auto tryMacros() -> IoTask<int> {
    IoResult<int> res = 42;
    ILIAS_CO_TRY(auto val, res);
    co_return val;
}

auto sendMessage() -> Task<void> {
    auto [sender, receiver] = mpsc::channel<int>(100);
    auto _ = co_await sender.send(42);
    if (co_await receiver.recv() != 42) {
        std::println("Bad !!!");
    }    
}

// This is a test program for the ilias::Task class.
auto mainTask() -> Task<int> {
    co_await sendMessage();
    if (co_await tryMacros() != 42) {
        std::println("Bad !!!");
    }
    co_await sleep(1000ms);
    std::println("Hello, world! from main Tasks!!! {}", IPAddress4::none());

    // When all
    std::vector<Task<void>> tasks;
    for (int i = 0; i < 10; ++i) {
        tasks.emplace_back(sleep(std::chrono::milliseconds{i}));
    }

    co_await TcpListener::bind("127.0.0.1:0");
    co_return 0;
}

int main() {
    ilias::PlatformContext ctxt;
    ilias::TracingWebUi webui;
    ctxt.install();
    webui.install();
    return mainTask().wait();
}