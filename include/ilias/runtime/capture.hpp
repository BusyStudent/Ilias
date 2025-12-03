// INTERNAL !!!
#pragma once

#include <ilias/defines.hpp>
#include <source_location>
#include <variant> // std::monostate
#include <vector>
#include <string>

ILIAS_NS_BEGIN

namespace runtime {

/**
 * @brief The virtual stack frame for coroutines
 * 
 */
class StackFrame {
public:
    StackFrame() = default;
    StackFrame(const StackFrame &) = default;
    StackFrame(std::string_view msg, std::source_location where) : 
        mMsg(msg), 
        mFilename(where.file_name()), 
        mFunction(where.function_name()), 
        mLine(where.line()) 
    {

    }

    StackFrame(std::source_location where) : StackFrame({}, where) {}

    /**
     * @brief Get the extra message for debugging (it may be empty)
     * 
     * @return std::string_view
     */
    auto message() const noexcept { return std::string_view(mMsg); }

    /**
     * @brief Get the filename of the frame
     * 
     * @return std::string_view
     */
    auto filename() const noexcept { return mFilename; }

    /**
     * @brief Get the function name of the frame
     * 
     * @return std::string_view
     */
    auto function() const noexcept { return mFunction; }

    /**
     * @brief Get the function name of the frame
     * 
     * @return size_t
     */
    auto line() const noexcept { return mLine; }

    /**
     * @brief Get the description of the frame
     * 
     * @return std::string 
     */
    auto toString() const -> std::string {
        auto msg = std::string {};
        msg += " at ";
        msg += mFilename;
        msg += ":";
        msg += std::to_string(mLine);
        msg += " (";
        msg += mFunction;
        msg += ")";
        if (!mMsg.empty()) {
            msg += " (";
            msg += mMsg;
            msg += ")";
        }
        return msg;
    }

    /**
     * @brief Set the Line number
     * 
     * @param line 
     */
    auto setLine(size_t line) -> void {
        mLine = line;
    }

    /**
     * @brief Set the extra message for debugging
     * 
     * @param msg 
     */
    auto setMessage(std::string_view msg) -> void {
        mMsg = msg;
    }
private:
    std::string          mMsg; // Extra message for debugging
    std::string_view     mFilename;
    std::string_view     mFunction;
    size_t               mLine = 0;
};

/**
 * @brief The full stack trace for coroutines
 * 
 */
class Stacktrace {
public:
    explicit Stacktrace(std::vector<StackFrame> frames) : mFrames(std::move(frames)) {}
    Stacktrace() = default;
    Stacktrace(const Stacktrace &) = default;
    Stacktrace(Stacktrace &&) = default;

    // For range for
    auto begin() const noexcept { return mFrames.begin(); }
    auto end() const noexcept { return mFrames.end(); }
    auto size() const noexcept { return mFrames.size(); }

    // Formatting
    auto toString() const -> std::string {
        auto str = std::string {};
        auto idx = size_t {0};
        if (mFrames.empty()) {
            str = "<Empty Stacktrace>";
            return str;
        }

        for (auto &frame : mFrames) {
            str += "#";
            str += std::to_string(idx);
            str += " ";
            str += frame.function();
            str += "\n";

            str += "      at ";
            str += frame.filename();
            str += ":";
            str += std::to_string(frame.line());

            auto msg = frame.message();
            if (!msg.empty()) {
                str += " [";
                str += msg;
                str += "]";
            }
            
            str += "\n";
            idx += 1;
        }
        return str;
    }

    auto operator [](size_t idx) const noexcept -> const StackFrame & { return mFrames[idx]; }
    auto operator =(const Stacktrace &) -> Stacktrace & = default;
    auto operator =(Stacktrace &&) -> Stacktrace & = default;
private:
    std::vector<StackFrame> mFrames;    
};

#if defined(ILIAS_CORO_TRACE)

/**
 * @brief Capture a source location, use `CaptureSouce loc = {}` to capture the current location
 * 
 */
class CaptureSource {
public:
    consteval CaptureSource(std::source_location loc = std::source_location::current()) noexcept : mLoc(loc) {}
    constexpr CaptureSource(const CaptureSource &) = default;

    auto toLocation() const noexcept { return mLoc; }
    operator std::source_location() const noexcept { return mLoc; }
private:
    std::source_location mLoc;
};

// Get the location from the capture source
inline auto toLocation(const CaptureSource &src) noexcept { return src.toLocation(); }

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
using runtime::Stacktrace;

ILIAS_NS_END

// Formatter
#if !defined(ILIAS_NO_FORMATTER)
ILIAS_FORMATTER(runtime::Stacktrace) {
    auto format(const auto &trace, auto &ctxt) const {
        return format_to(ctxt.out(), "{}", trace.toString());
    }
};
#endif // !defined(ILIAS_NO_FORMAT)