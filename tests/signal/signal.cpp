#include <ilias/platform.hpp>
#include <ilias/testing.hpp>
#include <ilias/signal.hpp>
#include <ilias/task.hpp>
#include <unistd.h>

using namespace ilias;
using namespace std::literals;

static constinit std::atomic<bool> alarmRaised {false};
static constinit std::atomic<bool> user1Raised {false};

static auto alarmHandler(int sig) -> void {
    alarmRaised.store(true, std::memory_order_relaxed);
}

static auto user1Handler(int sig, ::siginfo_t *, void *) -> void {
    user1Raised.store(true, std::memory_order_relaxed);
}

ILIAS_TEST(Signal, Core) {
    ::alarm(1);

    EXPECT_TRUE(co_await signal::waitFor(SIGALRM)); // Async wait for signal
    EXPECT_TRUE(alarmRaised.load(std::memory_order_relaxed)); // Should be chain
}

ILIAS_TEST(Signal, Cancel) {
    std::thread thread([]() {
        std::this_thread::sleep_for(1s);
        ::raise(SIGUSR1);
    });
    auto [ctrlC, user1] = co_await whenAny(
        signal::waitFor(SIGINT),
        signal::waitFor(SIGUSR1)
    );
    EXPECT_FALSE(ctrlC);
    EXPECT_TRUE(user1);
    EXPECT_TRUE(user1Raised.load(std::memory_order_relaxed));
    thread.join();
}

ILIAS_TEST(Signal, Invalid) {
    EXPECT_FALSE(co_await signal::waitFor(NSIG));
    EXPECT_FALSE(co_await signal::waitFor(-1));
    EXPECT_FALSE(co_await signal::waitFor(SIGKILL));
    EXPECT_FALSE(co_await signal::waitFor(SIGSTOP));
}

int main(int argc, char** argv) {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    ILIAS_TEST_SETUP_UTF8();

    // Prepare signal handler
    struct sigaction action {};
    action.sa_sigaction = user1Handler;
    action.sa_flags = SA_SIGINFO;
    ::sigaction(SIGUSR1, &action, nullptr);
    ::signal(SIGALRM, alarmHandler);

    // Prepare context
    PlatformContext ctxt;
    ctxt.install();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}