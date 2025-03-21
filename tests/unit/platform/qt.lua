
option("qt_test")
    set_default(false)
    set_description("Enable qt test")
option_end()

if has_config("qt_test") then
    add_requires("zlib")

    target("test_qt")
        set_default(false)
        add_rules("qt.widgetapp")
        add_files("qt.cpp")
        add_files("qt.ui")
        add_frameworks("QtCore", "QtWidgets", "QtGui")
        add_defines("ILIAS_ENABLE_LOG")
        add_packages("zlib")

        if has_config("use_openssl") then
            add_packages("openssl3")
        end
    target_end()
end
