add_requires("gtest")

-- Make all files in the unit directory into targets
for _, dir in ipairs(os.dirs("*")) do
    local name = path.basename(dir)
    local conf_path = path.join(dir, name .. ".lua")

    -- If this dir require a specific configuration, load it, and skip the auto target creation
    if os.exists(conf_path) then 
        includes(conf_path)
        goto continue
    end

    -- Otherwise, create a target for this file, in most case, it should enough
    target("test_" .. name)
        set_kind("binary")
        set_default(false)

        add_files(path.join(dir, "**.cpp"))
        add_tests(name, {run_timeout = 30000, packages = "gtest"})
        add_packages("gtest")
        add_deps("ilias")
    target_end()

    ::continue::
end
