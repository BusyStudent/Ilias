#include <ilias/platform.hpp>
#include <ilias/process.hpp>
#include <ilias/testing.hpp>
#include <ilias/fs/pipe.hpp>
#include <gtest/gtest.h>
#include <iostream>

using namespace ilias;

ILIAS_TEST(Process, SpawnFailed) {
    auto proc = Process::Builder {"non-existing-command"}.spawn();
    EXPECT_FALSE(proc.has_value());
    co_return;
}

ILIAS_TEST(Process, Spawn) {
    auto output = co_await Process::Builder {"powershell"}
        .args({"-Command", "ls"})
        .output();
    EXPECT_TRUE(output.has_value());
    std::cout << output->cout << std::endl;
}

int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_TEST_SETUP_UTF8();
    PlatformContext ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}