set_languages("c++latest")

target("ilias")
    set_kind("headeronly")
    add_headerfiles("ilias/**.hpp")
target_end()