function check_macros(name, macros, opt)
    opt = opt or {}
    local snippets = {}
    interp_save_scope()
    option(name)
        set_showmenu(false)
        before_check(function (option)
            if is_config("runtimes", "c++_shared", "c++_static") then
                option:add("cxflags", "-stdlib=libc++")
            end
        end)
        for _, macro in ipairs(macros) do
            if macro:find(' ', 1, true) then
                table.insert(snippets, ([[
                #if %s
                #else
                #   #error %s is not satisfied!
                #endif
                ]]):format(macro, macro))
            else
                table.insert(snippets, ([[
                #if%s %s
                #else
                #   #error %s is not defined!
                #endif
                ]]):format(opt.defined ~= false and "def" or "ndef", macro, macro))
            end
        end
        if opt.languages and opt.languages:startswith("c++") then
            add_cxxsnippets(name, table.concat(snippets, "\n"))
        else
            add_csnippets(name, table.concat(snippets, "\n"))
        end
        if opt.languages then
            set_languages(opt.languages)
        end
        if opt.defines then
            add_defines(opt.defines)
        end
        if opt.cflags then
            add_cflags(opt.cflags)
        end
        if opt.cxflags then
            add_cxflags(opt.cxflags)
        end
        if opt.cxxflags then
            add_cxxflags(opt.cxxflags)
        end
        if opt.languages and opt.languages:startswith("c++") then
            add_cxxincludes(opt.includes)
        else
            add_cincludes(opt.includes)
        end
    option_end()
    interp_restore_scope()
    add_options(name)
end