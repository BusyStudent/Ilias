#include <ilias/platform.hpp>
#include <ilias/testing.hpp>
#include <ilias/fs.hpp>
#include <filesystem>

using namespace ilias;
using namespace ilias::literals;

ILIAS_TEST(Fs, OpenNotExists) {
    EXPECT_FALSE(co_await File::open("./unknown_file"));
}

ILIAS_TEST(Fs, ReadWrite) {
    struct Guard {
        ~Guard() {
            std::filesystem::remove("./test_file");
        }
    } guard;

    // Test Create
    {
        auto file = (co_await File::open("./test_file", OpenOptions::WriteOnly)).value();
        auto ok = co_await file.writeAll("Hello world!"_bin);
        EXPECT_TRUE(ok);
    }

    {
        auto file = (co_await File::open("./test_file", OpenOptions::ReadOnly)).value();
        auto content = std::string {};
        auto ok = co_await file.readToEnd(content);
        EXPECT_TRUE(ok);
        EXPECT_EQ(content, "Hello world!");
    }

    // Test Append
    {
        auto opts = OpenOptions {}.append(true).create(true);
        auto file = (co_await File::open("./test_file", opts)).value();
        auto ok = co_await file.writeAll("Hello again!"_bin);
        EXPECT_TRUE(ok);
        EXPECT_TRUE(co_await file.flush());
    }

    {
        auto file = (co_await File::open("./test_file", OpenOptions::ReadOnly)).value();
        auto content = std::string {};
        auto ok = co_await file.readToEnd(content);
        EXPECT_TRUE(ok);
        EXPECT_EQ(content, "Hello world!Hello again!");
        EXPECT_EQ(co_await file.size(), 24);
    }
}


int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_TEST_SETUP_UTF8();
    PlatformContext ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}