if is_plat("windows") then
    add_requires("openssl")
end

-- Special configuration for https
if is_plat("windows") then
    target("test_https")
        set_kind("binary")
        set_default(false)

        add_files("https.cpp")
        add_tests("https")
        add_packages("gtest")
        add_defines("ILIAS_ENABLE_LOG")

        if is_plat("windows") then
            add_packages("openssl")
        end
    target_end()
end