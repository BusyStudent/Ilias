add_requires("gtest")

option("use_fmt")
    set_default(false)
    set_showmenu(true)
    set_description("Use fmt for logging")
option_end()

option("use_io_uring")
    set_default(false)
    set_showmenu(true)
    set_description("Use io uring as platform context")
option_end()

option("task_trace")
    set_default(false)
    set_showmenu(true)
    set_description("Add task stacktrace for debug use")
option_end()

option("use_openssl")
    set_default(not is_host("windows"))
    set_showmenu(true)
    set_description("Use openssl for the ssl backend")
option_end()

if has_config("use_fmt") then
    add_requires("fmt")
    add_packages("fmt")
    add_defines("ILIAS_USE_FMT")
end

if has_config("use_io_uring") then
    add_requires("liburing");
    add_packages("liburing");
    add_defines("ILIAS_USE_IO_URING")
end

if has_config("task_trace") then 
    add_defines("ILIAS_TASK_TRACE")
end

if has_config("use_openssl") then
    add_requires("openssl")
    add_defines("ILIAS_USE_OPENSSL")
end

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
    target_end()

    ::continue::
end