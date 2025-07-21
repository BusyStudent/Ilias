// INTERNAL !!!
#pragma once

#include <ilias/defines.hpp>
#include <stop_token>
#include <memory>

ILIAS_NS_BEGIN

namespace runtime {

using StopToken = std::stop_token;
using StopSource = std::stop_source;

template <typename Callable>
using StopCallback = std::stop_callback<Callable>;

/// Helper class for using std::stop_callback on Awaiter
class StopRegistration {
public:
    template <typename Callable>
    StopRegistration(const StopToken &token, Callable &&callback) {
        value = std::make_unique<Impl<Callable> >(token, std::forward<Callable>(callback));
    }
    StopRegistration(const StopRegistration &) = default;
    StopRegistration(StopRegistration &&) = default;
    StopRegistration() = default;
    ~StopRegistration() = default;

    StopRegistration &operator=(const StopRegistration &) = default;
    StopRegistration &operator=(StopRegistration &&) = default;
private:
    struct Base {
        virtual ~Base() = default;
    };

    template <typename Callable>
    struct Impl : Base {
        Impl(const StopToken &token, Callable &&callback) : callback(token, std::forward<Callable>(callback)) { }

        std::stop_callback<Callable> callback;
    };

    std::unique_ptr<Base> value;
};

} // namespace runtime

ILIAS_NS_END