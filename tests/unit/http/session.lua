if not is_plat("windows") then
    add_requires("openssl")
end

add_requires("zlib")

-- Special configuration for http session
target("test_session")
    set_kind("binary")
    set_default(false)

    add_files("session.cpp")
    add_tests("session", {run_timeout = 10000})
    add_packages("gtest")
    add_defines("ILIAS_ENABLE_LOG")
    add_packages("zlib")

    if not is_plat("windows") then
        add_packages("openssl")
    end
target_end()