#include <ilias/platform.hpp>
#include <ilias/console.hpp>
#include <ilias/task.hpp>

using namespace std::literals;

auto sub1() -> ilias::Task<void> {
    while (true) {
        co_await ilias::sleep(1s);
    }
}

auto fn() -> ilias::Task<void> {
    co_await ilias::whenAll(
        ilias::sleep(100h),
        sub1()
    );
}

void ilias_main() {
    ilias::TracingWebUi webui;
    webui.install();
    
    auto handle = ilias::spawn(fn());
    co_await std::move(handle);
}