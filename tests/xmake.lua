add_requires("gtest")

-- Make all files in the unit directory into targets
for _, file in ipairs(os.files("unit/**.cpp")) do
    local name = path.basename(file)
    target("test_" .. name)
        set_kind("binary")
        set_default(false)

        add_files(file)
        add_tests(name)
        add_packages("gtest")
        add_defines("ILIAS_ENABLE_LOG")
    target_end()
end