-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("CellLoadResearch")
set_version("0.8.24")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- add packages
add_requires("microsoft-detours")

-- define targets
target("CellLoadResearch")
    add_rules("commonlibsse-ng.plugin", {
        name = "CellLoadResearch",
        author = "Yurii",
        description = "Traces how Skyrim AE loads cells and locations during transitions"
    })

    add_packages("microsoft-detours")

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
