add_requires("gtest")

-- Make all files in the unit directory into targets
for _, file in ipairs(os.files("unit/**.cpp")) do
    local name = path.basename(file)
    local dir = path.directory(file)
    local conf_path = dir .. "/" .. name .. ".lua"

    -- If this file require a specific configuration, load it, and skip the auto target creation
    if os.exists(conf_path) then 
        includes(conf_path)
        goto continue
    end

    -- Otherwise, create a target for this file, in most case, it should enough
    target("test_" .. name)
        set_kind("binary")
        set_default(false)

        add_files(file)
        add_tests(name, {run_timeout = 10000})
        add_packages("gtest")
        add_defines("ILIAS_ENABLE_LOG")
        add_deps("ilias")
    target_end()

    ::continue::
end