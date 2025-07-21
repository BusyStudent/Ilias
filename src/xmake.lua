set_languages("c++23")

option("fmt")
    set_default(false)
    set_showmenu(true)
    set_description("Use fmt for logging")
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

option("use_openssl")
    set_default(not is_host("windows"))
    set_showmenu(true)
    set_description("Use openssl for the ssl backend")
option_end()

-- Add packages if 
if has_config("fmt") then
    add_requires("fmt")
end

if has_config("io_uring") then
    add_requires("liburing")
end

if has_config("use_openssl") then
    add_requires("openssl3")
end

target("ilias")
    set_kind("shared")
    set_configdir("../include/ilias/detail/")
    add_configfiles("../include/ilias/detail/config.hpp.in")
    add_headerfiles("(../include/ilias/**.hpp)")
    add_includedirs("../include", {public = true})
    add_defines("_ILIAS_SOURCE")
    add_files("**.cpp")

    -- Add links by platform
    if is_plat("windows") or is_plat("mingw") or is_plat("msys") then 
        add_syslinks("ws2_32", "bcrypt", {public = true})
    end

    -- Set var if
    if has_config("fmt") then
        add_packages("fmt", {public = true})
        set_configvar("ILIAS_USE_FMT", 1)
    end

    if has_config("io_uring") then
        add_packages("liburing", {public = true})
        set_configvar("ILIAS_USE_IO_URING", 1)
    end

    if has_config("task_trace") then
        set_configvar("ILIAS_CORO_TRACE", 1)
    end
target_end()