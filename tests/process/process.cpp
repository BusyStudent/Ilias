#include <ilias/platform.hpp>
#include <ilias/process.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>
#include <iostream>

using namespace ILIAS_NAMESPACE;

ILIAS_TEST(Process, SpawnFailed) {
    auto proc = Process::spawn("nonexistingcommand", {}, Process::RedirectAll);
    EXPECT_FALSE(proc.has_value());
    co_return;
}

ILIAS_TEST(Process, Spawn) {
#if defined(_WIN32)
    auto proc = Process::spawn("powershell", {"-Command", "ls"}, Process::RedirectAll).value();
#else
    auto proc = Process::spawn("ls", {"-l"}, Process::RedirectAll).value();
#endif
    EXPECT_TRUE(co_await proc.wait());
    std::string content;
    EXPECT_TRUE(co_await proc.out().readToEnd(content));
    std::cout << content << std::endl;
}

int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_TEST_SETUP_UTF8();
    PlatformContext ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}