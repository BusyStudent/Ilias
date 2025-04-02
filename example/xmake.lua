if has_config("use_fmt") then
    add_requires("fmt")
    add_packages("fmt")
end

if has_config("use_io_uring") then
    add_requires("liburing")
    add_packages("liburing")
end

target("httpserver")
    set_kind("binary")
    set_default(false)
    add_files("httpserver.cpp")
    add_defines("ILIAS_ENABLE_LOG")
target_end()

target("console_echo")
    set_kind("binary")
    set_default(false)
    add_files("console_echo.cpp")
    add_defines("ILIAS_ENABLE_LOG")
target_end()

target("delegate_ctxt")
    set_kind("binary")
    set_default(false)
    add_files("delegate_ctxt.cpp")
    add_defines("ILIAS_ENABLE_LOG")
    add_links("user32")
target_end()