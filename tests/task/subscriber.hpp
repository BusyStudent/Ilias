#pragma once

#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <iostream>

using namespace ilias;


// For unit tests
class Subscriber : public runtime::TracingSubscriber {
public:
    auto onTaskSpawn(runtime::CoroContext &ctxt) noexcept -> void override {
        ILIAS_INFO("Subscriber", "Spawned {}", static_cast<void *>(std::addressof(ctxt)));
    }

    auto onTaskComplete(runtime::CoroContext &ctxt) noexcept -> void override {
        ILIAS_INFO("Subscriber", "Completed {}", static_cast<void *>(std::addressof(ctxt)));
    }

    auto onResume(runtime::CoroContext &ctxt) noexcept -> void override {
        ILIAS_INFO("Subscriber", "Resumed {}", static_cast<void *>(std::addressof(ctxt)));
    }

    auto onChildBegin(runtime::CoroContext &child) noexcept -> void override {
        ILIAS_INFO("Subscriber", "Child begin {}", static_cast<void *>(std::addressof(child)));
    }

    auto onChildEnd(runtime::CoroContext &child) noexcept -> void override {
        ILIAS_INFO("Subscriber", "Child end {}", static_cast<void *>(std::addressof(child)));
    }
};