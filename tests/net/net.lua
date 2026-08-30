-- Main platform
target("test_net")
    set_kind("binary")
    set_default(false)

    add_files("*.cpp")
    add_tests("net", {run_timeout = 30000, packages = "gtest"})
    add_packages("gtest")
    add_deps("ilias")
target_end()

-- Qt loop
if has_config("qt_interop") then
    add_requires("qt6base")
    target("test_net_qt")
        add_rules("qt.console")
        set_default(false)

        add_defines("ILIAS_TEST_USE_QT")
        add_files("*.cpp")
        add_tests("net", {run_timeout = 30000, packages = "gtest"})
        add_packages("gtest")
        add_deps("ilias")
        add_frameworks("QtCore")
    target_end()
end