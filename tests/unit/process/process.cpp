#include <ilias/process/process.hpp>
#include <ilias/platform.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

#if defined(_WIN32) // Currently only implemented for Windows
TEST(Process, Spawn) {
    auto fn = []() -> Task<void> {
        auto proc = co_await Process::spawn("powershell", {"-Command", "ls"});
        if (proc) {
            co_await proc->join();
        }
    };
    fn().wait();
}
#endif // defined(_WIN32)

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    PlatformContext ctxt;
    return RUN_ALL_TESTS();
}