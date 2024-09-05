#include <ilias/platform/platform.hpp>
#include <ilias/task/when_any.hpp>
#include <ilias/fs/console.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(ConsoleTest, Open) {
    auto ctxt = IoContext::currentThread();
    auto out = Console::fromStdout(*ctxt);
    ASSERT_TRUE(out);

    auto ret = out->puts("HelloWorld\n").wait();
    ASSERT_TRUE(ret);
}

TEST(ConsoleTest, CancelRead) {

#if defined(_WIN32)
    auto inHandle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (inHandle == INVALID_HANDLE_VALUE || inHandle == nullptr) {
        GTEST_SKIP() << "No stdin";
    }
#endif

    auto ctxt = IoContext::currentThread();
    auto in = Console::fromStdin(*ctxt);
    if (!in) {
        std::cout << in.error().toString() << std::endl;
    }
    ASSERT_TRUE(in);

    char buffer[1024];

    auto [ret1, ret2] = whenAny(sleep(20ms), in->read(makeBuffer(buffer))).wait();
    ASSERT_TRUE(ret1);
    ASSERT_FALSE(ret2);
}

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    PlatformContext ctxt;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}