set_project("ilias")
add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage", "mode.asan")

set_languages("c++23")
set_encodings("utf-8")

includes("./src")
includes("./tests")