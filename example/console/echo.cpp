#include <ilias/fs/console.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/platform.hpp>
#include <ilias/signal.hpp>
#include <ilias/task.hpp>

using namespace ilias;
using namespace ilias::literals;

auto loop() -> Task<void> {
    auto out = (co_await Console::fromStdout()).value();
    auto in = (co_await Console::fromStdin()).value();
    auto reader = BufReader(std::move(in));
    while (true) {
        auto line = co_await reader.getline();
        if (!line) {
            break;
        }
        if (line->ends_with("\r")) {
            line->pop_back();
        }
        (co_await out.writeAll(makeBuffer(*line))).value();
        (co_await out.writeAll("\n"_bin)).value();
        (co_await out.flush()).value();
    }
}

void ilias_main() {
    auto [_, ctrlc] = co_await whenAny(loop(), signal::ctrlC());
    if (ctrlc) {
        ::puts("CTRL-C\n");
    }
}