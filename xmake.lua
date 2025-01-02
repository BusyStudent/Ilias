add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")

set_languages("c++20")
add_includedirs("./include")

includes("./include")
includes("./example")
includes("./tests")