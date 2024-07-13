
target("test_httpserver")
    set_kind("binary")
    add_files("test_httpserver.cpp")
target_end()

add_requires("gtest")
target("test_addr")
    set_kind("binary")
    add_files("test_addr.cpp")
    add_tests("addr")
    add_packages("gtest")
target_end()

target("test_async")
    set_kind("binary")
    add_files("test_async.cpp")
    add_tests("async")
target_end()

add_requires("gtest")
target("test_co")
    set_kind("binary")
    add_files("test_co.cpp")
    add_tests("co")
    add_packages("gtest")
target_end()

add_requires("gtest")
target("test_sync")
    set_kind("binary")
    add_files("test_sync.cpp")
    add_tests("sync")
    add_packages("gtest")
target_end()

target("test_dns")
    set_kind("binary")
    add_files("test_dns.cpp")
    add_tests("dns")
target_end()

add_requires("gtest")
target("test_expected")
    set_kind("binary")
    add_packages("gtest")
    add_files("test_expected.cpp")
    add_tests("expected")
target_end()

add_requires("gtest")
add_requires("zlib")
target("test_http")
    set_kind("binary")
    add_packages("gtest")
    add_packages("zlib")
    add_files("test_http.cpp")
    add_tests("http")

    if is_plat("linux") then 
        add_packages("openssl")
    end
target_end()

target("test_ssl")
    set_kind("binary")
    add_files("test_ssl.cpp")
    add_tests("ssl")

    if is_plat("linux") then 
        add_packages("openssl")
    end
target_end()

target("test_ring")
    set_kind("binary")
    add_files("test_ring.cpp")
    add_tests("ring")

    add_packages("gtest")
target_end()

if has_config("enable_qt") then 
    add_requires("zlib")
    target("test_qt")
        add_rules("qt.widgetapp")
        add_files("test_qt.cpp")
        add_files("test_qt.ui")
        add_frameworks("QtCore", "QtWidgets", "QtGui")
        add_packages("zlib")

        if is_plat("linux") then 
            add_packages("openssl")
        end
    target_end()
end
