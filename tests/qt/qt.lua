if has_config("qt_interop") then
    target("test_qt")
        set_default(false)
        add_rules("qt.console")
        add_files("test_qt.cpp")
        add_files("test_qt.hpp")
        add_deps("ilias")
        add_frameworks("QtCore", "QtTest", "QtNetwork")
        add_tests("qt", {run_timeout = 10000})
    target_end()
end