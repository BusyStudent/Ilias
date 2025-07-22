set_languages("c++23")

option("fmt")
    set_default(false)
    set_showmenu(true)
    set_description("Use fmt replace std::format")
option_end()

option("log")
    set_default(false)
    set_showmenu(true)
    set_description("Enable logging")
option_end()

option("spdlog")
    set_default(false)
    set_showmenu(true)
    set_description("Use spdlog for logging")
option_end()

option("io_uring")
    set_default(false)
    set_showmenu(true)
    set_description("Use io uring as platform context")
option_end()

option("task_trace")
    set_default(false)
    set_showmenu(true)
    set_description("Add task stacktrace for debug use")
option_end()

option("io")
    set_default(true)
    set_showmenu(true)
    set_description("Enable io support")
option_end()

-- Add packages if 
if has_config("fmt") then
    add_requires("fmt")
end

if has_config("io_uring") then
    add_requires("liburing")
end

if has_config("spdlog") and has_config("log") then
    add_requires("spdlog")
end

target("ilias")
    set_kind("shared")
    set_configdir("../include/ilias/detail/")
    add_configfiles("../include/ilias/detail/config.hpp.in")
    add_headerfiles("(../include/ilias/**.hpp)")
    add_includedirs("../include", {public = true})
    add_defines("_ILIAS_SOURCE")
    add_files("*.cpp")

    -- Add links by platform
    if is_plat("windows") or is_plat("mingw") or is_plat("msys") then 
        add_files("win32/*.cpp")
        add_syslinks("ws2_32", "bcrypt", {public = true})
    end

    -- Set var if
    if has_config("fmt") then
        add_packages("fmt", {public = true})
        set_configvar("ILIAS_USE_FMT", 1)
    end

    if has_config("log") then
        set_configvar("ILIAS_USE_LOG", 1)
    end

    if has_config("log") and has_config("spdlog") then
        add_packages("spdlog", {public = true})
        set_configvar("ILIAS_USE_SPDLOG", 1)
    end

    if has_config("io_uring") then
        add_packages("liburing", {public = true})
        set_configvar("ILIAS_USE_IO_URING", 1)
    end

    if has_config("task_trace") then
        set_configvar("ILIAS_CORO_TRACE", 1)
    end

    -- Default verison 0.3.0
    set_configvar("ILIAS_VERSION_MAJOR", "0")
    set_configvar("ILIAS_VERSION_MINOR", "3")
    set_configvar("ILIAS_VERSION_PATCH", "0")
target_end()