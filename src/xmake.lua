set_warnings("all")

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

    if not has_config("has_std_expected") then
        add_packages("zeus_expected", {public = true})
    end

    if has_config("fiber") then
        add_files("fiber/*.cpp")
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
target_end()