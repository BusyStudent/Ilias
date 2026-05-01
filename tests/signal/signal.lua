if is_plat("linux") then
    target("test_signal")
        set_default(false)
        set_kind("binary")
        
        add_deps("ilias")
        add_files("signal.cpp")
        add_packages("gtest")
        add_tests("test_signal", {run_timeout = 10000})
    target_end()
end