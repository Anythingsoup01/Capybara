Workspace = {
    name = "Capybara",
    flags = {
        "-fPIC",
    },
}

Project = {
    name = "Capybara",
    kind = "StaticLib",
    language = "C++",
    dialect = "20",

    pch = "pch/cpypch.h",

    files = {
        "src/*.cpp",
        "include/capybara/*.h",
        "internal/*.h",
    },

    includedirs = {
        "include",
        "internal",
        "pch"
    },

    links = {
        "dl",
        "elf",
        "elf++",
        "dwarf++",
        "ffi"
    },
    flags = {
        "-rdynamic",
    },

}

Project = {
    name = "test-linux-exe",
    kind = "ConsoleApp",
    language = "C++",
    dialect = "20",

    files = {
        "./test/linux/main.cpp"
    },

    includedirs = {
        "test/linux",
        "include",
    },

    links = {
        "Capybara"
    },
    flags = {
        "-rdynamic",
    },
}

External = "test/dll"
