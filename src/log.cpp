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
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    auto getLevelColor(LogLevel level) -> const char * {
        switch (level) {
            case LogLevel::Trace: return "\033[38;5;244m"; 
            case LogLevel::Debug: return "\033[38;5;75m";  
            case LogLevel::Info:  return "\033[38;5;113m";  
            case LogLevel::Warn:  return "\033[38;5;220m"; 
            case LogLevel::Error: return "\033[38;5;196m"; 
            default: return "\033[0m";
        }
    }
}

auto check(LogLevel level, std::string_view mod) -> bool {
    auto &ctxt = instance();
    // Filter...
    if (int(level) < int(ctxt.level)) {
        return false;
    }
    if (ctxt.blacklist.contains(mod)) {
        return false;
    }
    if (!ctxt.whitelist.empty() && !ctxt.whitelist.contains(mod)) {
        return false;
    }
    return true;
}

auto write(LogLevel level, std::string_view mod, std::source_location where, std::string_view content) -> void {
    auto &ctxt = instance();    

#if defined(ILIAS_USE_SPDLOG)
    // Forward to spdlog
    spdlog::source_loc loc(where.file_name(), where.line(), where.function_name());
    spdlog::level::level_enum spdlog_level = spdlog::level::info;
    switch (level) {
        case LogLevel::Trace: spdlog_level = spdlog::level::trace; break;
        case LogLevel::Debug: spdlog_level = spdlog::level::debug; break;
        case LogLevel::Info:  spdlog_level = spdlog::level::info;  break;
        case LogLevel::Warn:  spdlog_level = spdlog::level::warn;  break;
        case LogLevel::Error: spdlog_level = spdlog::level::err;   break;
        case LogLevel::Off: spdlog_level = spdlog::level::off;   break;
    }
    spdlog::log(loc, spdlog_level, "[{}] {}", mod, content);
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