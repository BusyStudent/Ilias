if has_config("qt_interop") then
    target("example_qt")
        set_default(false)
        add_rules("qt.widgetapp")
        add_files("qt.cpp")
        add_files("qt.ui")
        add_frameworks("QtCore", "QtWidgets", "QtGui", "QtNetwork")
        add_deps("ilias")
    target_end()
end