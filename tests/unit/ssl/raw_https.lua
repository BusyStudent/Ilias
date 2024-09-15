if not is_plat("windows") then
    add_requires("openssl")
end

-- Special configuration for https
if is_plat("windows") then
    target("test_raw_https")
        set_kind("binary")
        set_default(false)

        add_files("raw_https.cpp")
        add_tests("raw_https", {run_timeout = 10000})
        add_packages("gtest")
        add_defines("ILIAS_ENABLE_LOG")

        if not is_plat("windows") then
            add_packages("openssl")
        end
    target_end()
end