#include <ilias/fs/console.hpp>
#include <ilias/platform.hpp>

using namespace ILIAS_NAMESPACE;

void ilias_main() {
    auto in = co_await Console::fromStdin();
    auto out = co_await Console::fromStdout();
    if (!in || !out) {
        co_return;
    }
    while (true) {
        auto str = co_await in->getline();
        if (!str) {
            break;
        }
        co_await out->write(makeBuffer(str.value()));
        co_await out->puts("\n");
    }
}