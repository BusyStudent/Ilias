option("benchmark")
    set_default(false)
    set_showmenu(true)
    set_description("Enable benchmark")
option_end()

if has_config("benchmark") then
    add_requires("asio")
    target("asio_server")
        set_kind("binary")
        add_files("asio_server.cpp")
        add_packages("asio")
    target_end()

    target("ilias_server")
        set_kind("binary")
        add_files("ilias_server.cpp")
        add_deps("ilias")
    target_end()
end