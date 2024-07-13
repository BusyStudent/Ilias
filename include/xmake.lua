set_languages("c++latest")

target("ilias")
    set_kind("headeronly")
    add_headerfiles("*.hpp")
    add_headerfiles("*.inl")
target_end()