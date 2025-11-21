set_project("ilias")
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage", "mode.asan")

set_languages("c++23")
set_encodings("utf-8")

option("dev")
    set_default(false)
    set_showmenu(true)
    set_description("Enable dev mode, we are debugging")
option_end()

if has_config("dev") then
    if is_plat("linux") then 
        set_policy("build.sanitizer.address", true)
        set_policy("build.sanitizer.undefined", true)
    end
end

includes("./src")
includes("./tests")
includes("./example")
includes("./benchmark")