#include <ilias/process/process.hpp>
#include <ilias/platform.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(Process, Spawn) {
    auto fn = []() -> Task<void> {

#if defined(_WIN32)
        auto proc = co_await Process::spawn("powershell", {"-Command", "ls"});
#else
        std::vector<std::string_view> args;
        args.emplace_back("-l");
        auto proc = co_await Process::spawn("ls", args);
#endif
        if (proc) {
            co_await proc->join();
        }
    };
    fn().wait();
}

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    PlatformContext ctxt;
    return RUN_ALL_TESTS();
}