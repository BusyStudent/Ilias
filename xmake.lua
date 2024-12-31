set_project("ilias")
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")

set_languages("c++latest")
add_includedirs("./include")

includes("./include")
includes("./example")
includes("./tests")