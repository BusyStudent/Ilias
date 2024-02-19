add_rules("mode.debug", "mode.release")

target("test_addr")
    set_kind("binary")
    add_files("tests/test_addr.cpp")

target("test_co")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/test_co.cpp")

target("test_co")