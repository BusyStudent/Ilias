#include <ilias/platform.hpp>
#include <ilias/fs/pipe.hpp>
#include <ilias/buffer.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(PipeTest, Create) {
    auto ctxt = IoContext::currentThread();
    auto result = Pipe::pair(*ctxt);

    ASSERT_TRUE(result);
    auto &[sender, receiver] = *result;

    for (int i = 0; i < 1000; ++i) {
        auto ret = sender.writeAll(makeBuffer("Hello world!"sv)).wait();
        ASSERT_TRUE(ret);
        ASSERT_EQ(*ret, "Hello world!"sv.size());

        char buf["Hello world!"sv.size()];
        auto ret2 = receiver.readAll(makeBuffer(buf)).wait();
        ASSERT_TRUE(ret2);
        ASSERT_EQ(*ret2, "Hello world!"sv.size());
    }
}

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    PlatformContext ctxt;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}