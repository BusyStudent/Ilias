#pragma once

#include <ilias/runtime/coro.hpp>
#include <ilias/task/task.hpp>
#include <ilias/log.hpp>
#include <iostream>

using namespace ilias;


// For unit tests
class Subscriber : public runtime::TracingSubscriber {
public:
    auto onEvent(const runtime::TraceEvent &event) noexcept -> void override {
        ILIAS_INFO("Subscriber", "Event {} at {}", event.type, event.id);
    }
};