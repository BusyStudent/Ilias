set_languages("c++23")
set_warnings("all")

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

option("tls")
    set_default(true)
    set_showmenu(true)
    set_description("Enable tls support")
option_end()

option("openssl")
    set_default(false)
    set_showmenu(true)
    set_description("Always use openssl instead of native tls")
option_end()

option("cpp20")
    set_default(false)
    set_showmenu(true)
    set_description("Enable polyfills for std::expected in cpp20")
option_end()

option("fiber")
    set_default(true)
    set_showmenu(true)
    set_description("Enable stackful coroutine 'fiber' support")
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

option("coro_trace")
    set_default(false)
    set_showmenu(true)
    set_description("Add coroutine stacktrace for debug use")
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

if has_config("cpp20") then
    add_requires("zeus_expected")
end

if has_config("io_uring") then
    add_requires("liburing")
end

if has_config("tls") then
    -- Not in windows or force use openssl
    if not is_plat("windows") or has_config("openssl") then 
        add_requires("openssl3")
    end
end

if has_config("spdlog") and has_config("log") then
    add_requires("spdlog")
end

target("ilias")
    set_kind("$(kind)")
    set_configdir("../include/ilias/detail/")
    add_configfiles("../include/ilias/detail/config.hpp.in")
    add_headerfiles("(../include/ilias/**.hpp)")
    add_headerfiles("(../include/ilias_qt/**.hpp)")
    add_includedirs("../include", {public = true})
    add_defines("_ILIAS_SOURCE")
    
    -- Add source code
    add_files("net/*.cpp")
    add_files("*.cpp")

    -- Add module interface
    -- add_files("ilias.cppm", {public = true})

    -- Ignore specific warning for dllexport
    add_cxxflags("cl::/wd4251")
    add_cxxflags("cl::/wd4275")

    -- Add link and files by platform
    if is_plat("windows") or is_plat("mingw") or is_plat("msys") then 
        add_files("win32/*.cpp")
        add_syslinks("ws2_32", {public = true})
    end

    if is_plat("linux", "android") then
        if is_mode("release") then 
            set_symbols("hidden")
        end

        -- Add anl for no-android  
        if not is_plat("android") and not os.getenv("TERMUX_VERSION") then
            add_syslinks("anl") 
        end

        add_files("linux/*.cpp")
        add_syslinks("pthread", "dl", "rt", {public = true})
    end

    -- Set var if
    if has_config("fmt") then
        add_packages("fmt", {public = true})
        set_configvar("ILIAS_USE_FMT", 1)
    end

    if has_config("log") then
        set_configvar("ILIAS_USE_LOG", 1)
    end

    if has_config("tls") then
        set_configvar("ILIAS_TLS", 1)
        if is_plat("windows") and not has_config("openssl") then
            -- Is windows and not force use openssl, use schannel
            add_files("tls/schannel.cpp")
        else 
            add_packages("openssl3")
            add_files("tls/openssl.cpp")
        end
    end

    if has_config("log") and has_config("spdlog") then
        add_packages("spdlog", {public = true})
        set_configvar("ILIAS_USE_SPDLOG", 1)
    end

    if has_config("cpp20") then
        add_packages("zeus_expected", {public = true})
        set_configvar("ILIAS_CPP20", 1)
    end

    if has_config("fiber") then
        add_packages("fiber", {public = true})
        set_configvar("ILIAS_USE_FIBER", 1)
    end

    if has_config("io_uring") then
        add_packages("liburing", {public = true})
        set_configvar("ILIAS_USE_IO_URING", 1)
    end

    if has_config("coro_trace") then
        set_configvar("ILIAS_CORO_TRACE", 1)
    end

    -- Kind
    if is_kind("static") then 
        set_configvar("ILIAS_STATIC", 1)
    elseif is_kind("shared") then
        set_configvar("ILIAS_DLL", 1)
    end

    -- Default verison 0.3.3
    set_configvar("ILIAS_VERSION_MAJOR", 0)
    set_configvar("ILIAS_VERSION_MINOR", 3)
    set_configvar("ILIAS_VERSION_PATCH", 3)
target_end()