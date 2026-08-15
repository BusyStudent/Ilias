if has_config("modules") then
    target("test_modules")
        set_default(false)
        set_kind("binary")
        
        add_deps("ilias_modules")
        add_files("*.cpp")
        add_tests("modules", {run_timeout = 10000})

        if is_plat("mingw") then
            -- For std::print
            add_syslinks("stdc++exp")
        end
    target_end()
end