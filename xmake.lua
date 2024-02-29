add_rules("mode.debug", "mode.release")

if is_plat("linux") then 
    add_cxxflags("-fcoroutines")
    add_links("pthread")
end

target("test_addr")
    set_kind("binary")
    add_files("tests/test_addr.cpp")

target("test_async")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/test_async.cpp")

target("test_co")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/test_co.cpp")