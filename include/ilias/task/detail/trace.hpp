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
#include <set>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief Dump the stack trace to a string.
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
            "\033[33m#{}\033[0m \033[36m{}\033[0m (\033[32m{}:{}\033[0m)\n", 
            idx++, name, frame->file, frame->line
        );
    }
    return result;
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

ILIAS_NS_END

#else

ILIAS_NS_BEGIN

inline auto backtrace([[maybe_unused]] FILE *stream = nullptr) noexcept {
    return std::suspend_never { };
}

ILIAS_NS_END

#endif // defined(ILIAS_TASK_TRACE)