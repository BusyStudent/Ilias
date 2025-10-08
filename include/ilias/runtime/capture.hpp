// INTERNAL !!!
#pragma once

#include <ilias/defines.hpp>
#include <source_location>
#include <variant> // std::monostate
#include <vector>
#include <string>

ILIAS_NS_BEGIN

namespace runtime {

// The virtual stack frame for coroutines
class StackFrame {
public:
    StackFrame() = default;
    StackFrame(const StackFrame &) = default;
    StackFrame(void *ptr, std::string_view msg, std::source_location where) : mPtr(ptr), mMsg(msg), mWhere(where) {}

    auto address() const noexcept { return mPtr; }
    auto message() const noexcept { return std::string_view(mMsg); }
    auto location() const noexcept { return mWhere; }
    auto toString() const -> std::string {
        return std::string(mWhere.function_name()) + " at " + mWhere.file_name() + ":" + std::to_string(mWhere.line()) + " (" + mMsg + ")";
    }
private:
    void                *mPtr = nullptr; // The pointer to the coro frame (nullptr on virtual frame)
    std::string          mMsg; // Extra message for debugging
    std::source_location mWhere; // The location of the frame
};

#if defined(ILIAS_CORO_TRACE)

/**
 * @brief Capture a source location, use `CaptureSouce loc = {}` to capture the current location
 * 
 */
class CaptureSource {
public:
    constexpr CaptureSource(std::source_location loc = std::source_location::current()) noexcept : mLoc(loc) {}
    constexpr CaptureSource(const CaptureSource &) = default;

    auto toLocation() const noexcept { return mLoc; }
private:
    std::source_location mLoc;
};

// Get the location from the capture source
inline auto toLocation(const CaptureSource &src) noexcept { return src.toLocation(); }

using StackFrameVec = std::vector<StackFrame>;

#else

/**
 * @brief Capture a source location, currently disabled
 * 
 */
using CaptureSource = std::monostate;

/**
 * @brief The virtual stack frame vector for coroutines, currently disabled
 * 
 */
using StackFrameVec = std::monostate;

// Get the location from the capture source, no-op
consteval auto toLocation(const CaptureSource &) noexcept { return std::source_location {}; }

#endif // ILIAS_CORO_TRACE

} // namespace runtime

// Export to user
using runtime::StackFrame;

ILIAS_NS_END