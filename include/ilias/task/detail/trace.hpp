/**
 * @file trace.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-04-06
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include <ilias/task/detail/promise.hpp>
#include <ilias/task/detail/view.hpp>
#include <cstdio>

#if defined(ILIAS_TASK_TRACE)
#include <memory>
#include <set>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Dump the stack trace to a string. (from bottom to top)
 * 
 * @param handle 
 * @return std::string 
 */
inline auto backtraceImpl(CoroHandle handle) -> std::string {
    std::string result;
    size_t idx = 0;
    for (auto frame = &handle.frame(); frame != nullptr; frame = frame->parent) {
        auto name = frame->name;
#if defined(_MSC_VER)
        if (auto pos = name.find("__cdecl"); pos != std::string::npos) { // Skip the return type, (int __cdecl func(int))
            name = name.substr(pos + 8);
        }
#endif // defined(_MSC_VER)
        fmtlib::format_to(
            std::back_inserter(result), 
            "\033[33m#{}\033[0m \033[36m{}\033[0m (\033[32m{}:{}\033[0m){}\n", 
            idx++, name, frame->file, frame->line,
            frame->msg.empty() ? std::string("") : fmtlib::format(" \033[90m{}\033[0m", frame->msg)
        );
    }
    return result;
}

inline auto runningCoroutines() noexcept -> std::set<CoroHandle> & {
    static std::set<CoroHandle> coros;
    return coros;
}

inline auto dumpCoroutines(FILE *stream) noexcept -> void {
    if (!stream) {
        return;
    }
    ::fprintf(stream, "Dumping %d coroutines:\n", int(runningCoroutines().size()));
    for (auto &handle : runningCoroutines()) {
        auto frame = &handle.frame(); // TODO: Handle if has more than one child
        while (!frame->children.empty()) {
            frame = frame->children[0];
        }
        ::fprintf(stream, "Dumping coroutine %p:\n", std::coroutine_handle<>(handle).address());
        ::fprintf(stream, "%s", detail::backtraceImpl(handle).c_str());
    }
}

/**
 * @brief Install a trace frame for coroutine debugging
 * 
 * @param handle The coroutine handle
 * @param msg The extra message
 * @param where Source location information
 */
inline auto installTraceFrame(CoroHandle handle, std::string_view msg, std::source_location where = std::source_location::current()) noexcept -> void {
    auto frame = std::make_unique<StackFrame>();
    auto [it, emplaced] = runningCoroutines().emplace(handle);
    ILIAS_ASSERT(emplaced); // In most time, the handle should be unique.

    // Setup the frame and add it to the task
    frame->setLocation(where);
    frame->msg.assign(msg);
    handle.frame().parent = frame.get();
    handle.registerCallback([f = std::move(frame), it]() { // Install the cleanup callback
        runningCoroutines().erase(it);
    });
}


} // namespace detail

/**
 * @brief Dump the stack trace to the given stream.
 * 
 * @param stream The FILE* to dump the stack trace to.
 * @return auto 
 */
[[nodiscard("Don't forget to use co_await!!!")]]
inline auto backtrace(FILE *stream = stderr) noexcept {
    struct Awaiter {
        auto await_ready() const noexcept -> bool { return false; }
        auto await_suspend(CoroHandle handle) const noexcept -> bool {
            ::fprintf(stream, "%s", detail::backtraceImpl(handle).c_str());
            return false;
        }
        auto await_resume() const noexcept -> void { }
        FILE *stream;
    };
    return Awaiter { stream };
}

/**
 * @brief Dump the stack trace to the stderr.
 * 
 * @param stream The FILE* to dump the stack trace to.
 */
inline auto dumpCoroutines(FILE *stream = stderr) noexcept -> void {
    return detail::dumpCoroutines(stream);
}

ILIAS_NS_END

#else

ILIAS_NS_BEGIN

inline auto backtrace([[maybe_unused]] FILE *stream = nullptr) noexcept {
    return std::suspend_never { };
}

inline auto dumpCoroutines([[maybe_unused]] FILE *stream = nullptr) noexcept -> void { }

ILIAS_NS_END

#endif // defined(ILIAS_TASK_TRACE)