#include <ilias/platform/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/task/task.hpp>

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

class StdioTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists("./work")) {
            std::filesystem::create_directory("./work");
        }
        std::ofstream stdinfile(kStdinFile, std::ios::out);

        stdinfile.clear();
        stdinfile.close();

        freopen(kStdinFile, "r", stdin);

        ctxt       = IoContext::currentThread();
        stdindesc  = ctxt->addDescriptor(fileno(stdin), IoDescriptor::Unknown).value();
        stdoutdesc = ctxt->addDescriptor(fileno(stdout), IoDescriptor::Unknown).value();
    }

    auto ReadFromStdin(std::size_t size) -> Task<std::string> {
        int                    len = 0;
        std::vector<std::byte> data(size);
        while (len < size) {
            auto ret = co_await ctxt->read(stdindesc, {data.data() + len, size - len}, std::nullopt);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            len += ret.value();
        }
        co_return std::string {reinterpret_cast<char *>(data.data()), len};
    }

    auto writeToStdout(const char *data, size_t size) -> Task<void> {
        int len = 0;
        while (len < size) {
            auto ret = co_await ctxt->write(stdoutdesc, {reinterpret_cast<const std::byte *>(data + len), size - len},
                                            std::nullopt);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            len += ret.value();
        }
        co_return Result<void>();
    }

    auto writeToStdin(const char *data, size_t size) -> Task<void> {
        std::ofstream stdinfile(kStdinFile, std::ios::out);
        stdinfile << std::string_view(data, size);
        stdinfile.close();
        co_return Result<void>();
    }

    void TearDown() override {
        std::filesystem::remove(kStdinFile);
        std::filesystem::remove_all("./work");

        ctxt->removeDescriptor(stdindesc);
        ctxt->removeDescriptor(stdoutdesc);
    }

private:
    const char *kStdinFile = "./work/stdin.txt";

    ilias::IoContext *ctxt;
    IoDescriptor     *stdindesc;
    IoDescriptor     *stdoutdesc;
};

TEST_F(StdioTest, Stdin) {
    writeToStdin("hello world", 11).wait();
    auto res = ReadFromStdin(11).wait();
    ASSERT_STREQ(res.value_or("").c_str(), "hello world");

    auto ret = writeToStdout("this is a test message\n", 23).wait();
    ASSERT_TRUE(ret);
}

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    PlatformContext ctxt;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}