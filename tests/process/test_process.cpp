#include <ilias/platform.hpp>
#include <ilias/process.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>
#include <iostream>

namespace {
    std::string_view SHELL = 
#ifdef _WIN32
        "powershell"
#else
        "bash"
#endif // _WIN32
    ;
}

using namespace ilias;

ILIAS_TEST(Process, SpawnFailed) {
    auto proc = Process::Builder{"non-existing-command"}.spawn();
    EXPECT_FALSE(proc);
    co_return;
}

ILIAS_TEST(Process, Spawn) {

#if defined(_WIN32)
    auto output = co_await Process::Builder{"powershell"}
        .args({"-Command", "ls"})
        .output();
#else
    auto output = co_await Process::Builder{"ls"}
        .output();
#endif

    EXPECT_TRUE(output);
    std::cout << output->cout << std::endl;
}

ILIAS_TEST(Process, KillOnDestroy) {
    using namespace std::literals;
    Process::Builder{SHELL}
        .killOnDestroy(true)
        .spawn();
    co_await ilias::sleep(10ms); // Give the runtime sometime to clean up
}

ILIAS_TEST(Process, Kill) {
    auto proc = Process::Builder{SHELL}.spawn();
    EXPECT_TRUE(proc);
    EXPECT_TRUE(proc->kill());
    EXPECT_TRUE(co_await proc->wait());
}

ILIAS_TEST_MAIN() {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
}