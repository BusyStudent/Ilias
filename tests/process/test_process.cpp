#include <ilias/platform.hpp>
#include <ilias/process.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>
#include <iostream>

using namespace ilias;

ILIAS_TEST(Process, SpawnFailed) {
    auto proc = Process::Builder {"non-existing-command"}.spawn();
    EXPECT_FALSE(proc);
    co_return;
}

ILIAS_TEST(Process, Spawn) {

#if defined(_WIN32)
    auto output = co_await Process::Builder {"powershell"}
        .args({"-Command", "ls"})
        .output();
#else
    auto output = co_await Process::Builder {"ls"}
        .output();
#endif

    EXPECT_TRUE(output);
    std::cout << output->cout << std::endl;
}

ILIAS_TEST(Process, Kill) {

#if defined(_WIN32)
    auto proc = Process::Builder {"powershell"}.spawn();
#else
    auto proc = Process::Builder {"bash"}.spawn();
#endif

    EXPECT_TRUE(proc);
    EXPECT_TRUE(proc->kill());
    EXPECT_TRUE(co_await proc->wait());
}

ILIAS_TEST_MAIN() {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
}