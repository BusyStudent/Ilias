add_rules("mode.debug", "mode.release")

if is_plat("linux") then 
    add_cxxflags("-fcoroutines")
    add_links("pthread")
end

option("enable_qt")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Qt Depend")
option_end()

set_languages("c++latest")

target("test_httpserver")
    set_kind("binary")
    add_files("tests/test_httpserver.cpp")
target_end()

target("test_addr")
    set_kind("binary")
    add_files("tests/test_addr.cpp")
    add_tests("addr")
target_end()

target("test_async")
    set_kind("binary")
    add_files("tests/test_async.cpp")
    add_tests("async")
target_end()

target("test_co")
    set_kind("binary")
    add_files("tests/test_co.cpp")
    add_tests("co")
target_end()

target("test_dns")
    set_kind("binary")
    add_files("tests/test_dns.cpp")
    add_tests("dns")
target_end()

add_requires("gtest")
target("test_expected")
    set_kind("binary")
    add_packages("gtest")
    add_files("tests/test_expected.cpp")
    add_tests("expected")
target_end()

add_requires("openssl")
target("test_ssl")
    set_kind("binary")
    add_files("tests/test_ssl.cpp")
    add_tests("ssl")

    add_packages("openssl")
target_end()

add_requires("openssl")
target("test_ring")
    set_kind("binary")
    add_files("tests/test_ring.cpp")
    add_tests("ssl")
    set_group("ring")

    add_packages("openssl")
    add_packages("gtest")
target_end()

if has_config("enable_qt") then 
    target("test_qt")
        add_rules("qt.widgetapp")
        add_files("tests/test_qt.cpp")
        add_files("tests/test_qt.ui")
        add_frameworks("QtCore", "QtWidgets", "QtGui")
    target_end()
end
