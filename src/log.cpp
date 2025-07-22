#include <ilias/log.hpp>
#include <algorithm>
#include <string>
#include <chrono>
#include <set>

#if defined(ILIAS_USE_LOG)

#if defined(ILIAS_USE_SPDLOG)
    #include <spdlog/spdlog.h>
#endif

ILIAS_NS_BEGIN

namespace logging {

namespace {
    struct CaseCmp {
        using is_transparent = void;

        bool operator()(std::string_view a, std::string_view b) const {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char a, char b) {
                return std::tolower(a) < std::tolower(b);
            });
        }
    };

    struct Context {
        LogLevel level = LogLevel::Info;
        std::set<std::string, CaseCmp> whitelist;
        std::set<std::string, CaseCmp> blacklist;
        bool nolocation = false;
        bool notime = false;
    };

    auto instance() -> Context & {
        static Context ctxt;
        return ctxt;
    }

    auto getLevelString(LogLevel level) -> std::string_view {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    auto getLevelColor(LogLevel level) -> const char * {
        switch (level) {
            case LogLevel::Trace: return "\033[1;34m";
            case LogLevel::Info:  return "\033[1;32m";
            case LogLevel::Warn:  return "\033[1;33m";
            case LogLevel::Error: return "\033[1;31m";
            default: return "\033[1;30m";
        }
    }
}

auto write(LogLevel level, std::string_view mod, std::source_location where, std::string_view content) -> void {
    auto &ctxt = instance();
    
    // Filter...
    if (int(level) < int(ctxt.level)) {
        return;
    }
    if (ctxt.blacklist.contains(mod)) {
        return;
    }
    if (!ctxt.whitelist.empty() && !ctxt.whitelist.contains(mod)) {
        return;
    }

#if defined(ILIAS_USE_SPDLOG)
    // Forward to spdlog
    #error "spdlog is not supported yet"
#else // Use built-in implementation
    // Set the color
    std::string buf;
    buf += getLevelColor(level);

    // Print the body
    auto now = std::chrono::system_clock::now();
    auto file = std::string_view(where.file_name());
    auto func = std::string_view(where.function_name());
    auto line = where.line();

    // Remove the path, only keep the file name
    if (auto pos = file.find_last_of('/'); pos != std::string_view::npos) {
        file = file.substr(pos + 1);
    }
    if (auto pos = file.find_last_of('\\'); pos != std::string_view::npos) {
        file = file.substr(pos + 1);
    }
    if (!ctxt.notime) {
        fmtlib::format_to(std::back_inserter(buf), "[{}] ", now);
    }
    fmtlib::format_to(std::back_inserter(buf), "[{}/{}]: ", getLevelString(level), mod);
    if (!ctxt.nolocation) {
        fmtlib::format_to(std::back_inserter(buf), "[{}:{}] ", file, line);
    }

    // Clear the color
    buf += "\033[0m";
    buf += content;
    buf += "\n";

    ::fputs(buf.c_str(), stderr);
#endif

}

auto setLevel(LogLevel level) -> void {
    instance().level = level;
}

auto addWhitelist(std::string_view mod) -> void {
    instance().whitelist.insert(std::string(mod));
}

auto addBlacklist(std::string_view mod) -> void {
    instance().blacklist.insert(std::string(mod));
}

} // namespace logging

ILIAS_NS_END

#endif