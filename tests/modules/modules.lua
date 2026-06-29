if has_config("modules") then
    target("test_modules")
        set_default(false)
        set_kind("binary")
        
        add_deps("ilias_modules")
        add_files("test_modules.cpp")
        add_tests("signal", {run_timeout = 10000})
    target_end()
end