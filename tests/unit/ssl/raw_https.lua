-- Special configuration for https
if is_plat("windows") then
    target("test_raw_https")
        set_kind("binary")
        set_default(false)

        add_files("raw_https.cpp")
        add_tests("raw_https", {run_timeout = 10000})
        add_packages("gtest")
        add_defines("ILIAS_ENABLE_LOG")

        add_deps("ilias")
    target_end()
end