set_xmakever("3.0.0")
set_project("ilias")
set_version("0.4.0")

add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.coverage")

set_encodings("utf-8")

-- Options
option("dev",        {default = false,     description = "Enable dev mode, we are debugging"})
option("fmt",        {default = false,     description = "Use fmt replace std::format"})
option("log",        {default = false,     description = "Enable logging"})
option("openssl",    {default = false,     description = "Always use openssl instead of native tls"})
option("cpp20",      {default = false,     description = "Enable polyfills for std::expected in cpp20"})
option("spdlog",     {default = false,     description = "Use spdlog for logging"})
option("io_uring",   {default = false,     description = "Use io uring as platform context"})
option("coro_trace", {default = false,     description = "Add coroutine stacktrace for debug use"})
option("tls",        {default = true,      description = "Enable tls support"})
option("fiber",      {default = true,      description = "Enable stackful coroutine 'fiber' support"})

-- No-op Options (leave for compatibility)
option("io",         {default = true,      description = "Enable io support"})

-- Add packages if 
if has_config("fmt") then
    add_requires("fmt")
end

if has_config("cpp20") then
    add_requires("zeus_expected")
    set_languages("c++20")
else 
    set_languages("c++23")
end

if has_config("io_uring") then
    add_requires("liburing")
end

if has_config("tls") then
    -- Not in windows or force use openssl
    if not is_plat("windows") or has_config("openssl") then 
        add_requires("openssl3")
    end
end

if has_config("spdlog") and has_config("log") then
    add_requires("spdlog")
end

if has_config("dev") then
    if is_plat("linux") then 
        set_policy("build.sanitizer.address", true)
        set_policy("build.sanitizer.undefined", true)
    end
end

includes("./src")
includes("./tests")
includes("./example")
includes("./benchmark")