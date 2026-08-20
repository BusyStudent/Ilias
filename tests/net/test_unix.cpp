#include <ilias/net/unix.hpp>
#include <ilias/testing.hpp>

using namespace ilias;

ILIAS_TEST(Net, Unix) {
    UnixEndpoint endpoint{"test_sock"};
    auto listener = (co_await UnixListener::bind(endpoint)).value();
    auto stream = (co_await UnixStream::connect(endpoint)).value();

    auto [peer, addr] = (co_await listener.accept()).value();

    // Check endpoint
    EXPECT_EQ(endpoint, listener.localEndpoint());
    EXPECT_EQ(endpoint, stream.remoteEndpoint());

    // Test read write
    EXPECT_TRUE(co_await stream.writeUint64LE(42));
    EXPECT_EQ(co_await peer.readUint64LE(), 42);

    EXPECT_TRUE(co_await peer.writeUint64LE(42));
    EXPECT_EQ(co_await stream.readUint64LE(), 42);
}