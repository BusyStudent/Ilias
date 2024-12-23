set_languages("c++latest")

target("ilias")
    set_kind("headeronly")
    add_headerfiles("(ilias/**.hpp)")
    add_headerfiles("(ilias/**.cpp)")
target_end()