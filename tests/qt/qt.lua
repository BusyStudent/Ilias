if has_config("qt_interop") then
    add_requires("qt6base")
    target("test_qt")
        set_default(false)
        add_rules("qt.console")
        add_files("test_qt.cpp")
        add_files("test_qt.hpp")
        add_packages("qt6base")
        add_deps("ilias")
        add_frameworks("QtCore", "QtTest", "QtNetwork")
        add_tests("qt", {run_timeout = 10000})
    target_end()
end