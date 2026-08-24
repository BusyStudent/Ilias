if has_config("qt_interop") then
    add_requires("qt6base")
    target("example_qt")
        set_default(false)
        add_rules("qt.widgetapp")
        add_files("qt.cpp")
        add_files("qt.ui")
        add_packages("qt6base")
        add_frameworks("QtCore", "QtWidgets", "QtGui", "QtNetwork")
        add_deps("ilias")
    target_end()
end