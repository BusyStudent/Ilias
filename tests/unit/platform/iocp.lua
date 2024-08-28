-- Special configuration for iocp
if is_plat("windows") then
    target("test_iocp")
        set_kind("binary")
        set_default(false)

        add_files("iocp.cpp")
        add_tests("iocp")
        add_packages("gtest")
        add_defines("ILIAS_ENABLE_LOG")
    target_end()
end