#include <ilias/process/process.hpp>
#include <ilias/platform.hpp>
#include <iostream>

using namespace ILIAS_NAMESPACE;

void ilias_main() {
    auto proc = Process::spawn("powershell", {"-Command", "ls"}, Process::RedirectStdout).value();
    if (auto res = co_await proc.wait(); !res) {
        std::cout << "Error: " << res.error() << std::endl;
        co_return;
    }
    std::string content;
    co_await proc.out().readToEnd(content);
    std::cout << content << std::endl;
}