add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")

if is_plat("linux") then 
    add_cxxflags("-fcoroutines")
    add_requires("openssl")
    add_links("pthread")
end

option("enable_qt")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Qt Depend")
option_end()

option("asan_check")
    set_default(false)
    set_showmenu(true)
    set_description("Enable asan check")
option_end()

if has_config("asan_check") then 
    set_policy("build.sanitizer.address", true)
end

set_languages("c++latest")

includes("./include")
includes("./tests")