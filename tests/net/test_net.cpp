#include <ilias/platform/delegate.hpp>
#include <ilias/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/net.hpp>
#include <ilias/io.hpp>
#include <ilias/testing.hpp>
#include <gtest/gtest.h>

using namespace ilias;
using namespace ilias::literals;
using namespace std::literals;

ILIAS_TEST(Net, Pipe) {
    auto [writer, reader] = PipePair::make().value();
    for (auto val : std::views::iota(0, 100)) {
        EXPECT_TRUE(co_await writer.writeUint32LE(val));
    }
    for (auto val : std::views::iota(0, 100)) {
        EXPECT_EQ(co_await reader.readUint32LE(), val);
    }
}

#if defined(_WIN32)
ILIAS_TEST(Net, Win32Handle) {
    Win32Handle event {
        ::CreateEventW(nullptr, TRUE, FALSE, nullptr)
    };
    EXPECT_TRUE(event);

    // Start an thread to notify the event after 0ms, 10ms, 20ms, 30ms, ...
    for (auto val : std::views::iota(0, 10)) {
        std::thread thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(val * 10));
            ::SetEvent(event.get());
        });
        EXPECT_TRUE(co_await event);
        thread.join();
        ::ResetEvent(event.get());
    }

    // Test Cancel
    {
        auto handle = spawn(event.wait());
        co_await this_coro::yield();
        handle.stop();
        EXPECT_FALSE(co_await std::move(handle));
    }
}
#endif // _WIN32


class IoEventLoop : public ProxyContext {
public:
    auto post(void (*fn)(void*), void* arg) -> void override {
        mLoop.post(fn, arg);
    }

    auto run(runtime::StopToken token) -> void override {
        mLoop.run(std::move(token));
    }
private:
    EventLoop mLoop;
};


int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_TEST_SETUP_UTF8();
    IoEventLoop ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}