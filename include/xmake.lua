set_languages("c++latest")

option("use_fmt")
    set_default(false)
    set_showmenu(true)
    set_description("Use fmt for logging")
option_end()

option("use_io_uring")
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
if has_config("use_fmt") then
    add_requires("fmt")
end

if has_config("use_io_uring") then
    add_requires("liburing")
end

if has_config("use_openssl") then
    add_requires("openssl3")
end

target("ilias")
    set_kind("headeronly")
    set_configdir("ilias/detail/")
    add_configfiles("ilias/detail/config.hpp.in")
    add_headerfiles("(ilias/**.hpp)")

    -- Add links by platform
    if is_plat("windows") or is_plat("mingw") or is_plat("msys") then 
        add_syslinks("ws2_32")
    end

    -- Set var if
    if has_config("use_fmt") then
        add_packages("fmt", {public = true})
        set_configvar("ILIAS_USE_FMT", 1)
    end

    if has_config("use_io_uring") then
        add_packages("liburing", {public = true})
        set_configvar("ILIAS_USE_IO_URING", 1)
    end

    if has_config("use_openssl") then
        add_packages("openssl3", {public = true})
        set_configvar("ILIAS_USE_OPENSSL", 1)
    end

    if has_config("task_trace") then
        set_configvar("ILIAS_TASK_TRACE", 1)
    end
target_end()