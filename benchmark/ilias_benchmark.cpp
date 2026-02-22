#define ANKERL_NANOBENCH_IMPLEMENT
#include <ilias/platform.hpp>
#include <ilias/task.hpp>
#include <nanobench.h>

auto nop() -> ilias::Task<void> {
    co_return;
}

auto yield() -> ilias::Task<void> {
    co_await ilias::this_coro::yield();
}

auto main(int argc, char** argv) -> int {
    ilias::EventLoop ctxt;
    ctxt.install();

    ankerl::nanobench::Bench {}.run("Create no-op task", [&] {
        auto task = nop();
        ankerl::nanobench::doNotOptimizeAway(task);
    });

    ankerl::nanobench::Bench {}.run("Create and wait for no-op task", [&] {
        auto task = nop();
        task.wait();
    });

    ankerl::nanobench::Bench {}.run("Create and yield task", [&] {
        yield().wait();
    });
}