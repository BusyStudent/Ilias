if has_config("qt_interop") then
    target("test_qt")
        set_default(false)
        add_rules("qt.console")
        add_files("qtest.cpp")
        add_files("qtest.hpp")
        add_deps("ilias")
        add_frameworks("QtCore", "QtTest", "QtNetwork")
        add_tests("test_qt", {run_timeout = 10000})
    target_end()
end