import std;
import ilias;

using namespace ilias;
using namespace std::chrono_literals;

// This is a test program for the ilias::Task class.
auto mainTask() -> Task<int> {
    co_await sleep(1000ms);
    std::println("Hello, world! from main Tasks!!! {}", IPAddress4::none());
    co_return 0;
}

int main() {
    ilias::PlatformContext ctxt;
    ctxt.install();

    return mainTask().wait();
}