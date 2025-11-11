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

    pch = "include/Capybara/cpypch.h",

    files = {
        "./include/Capybara/*.cpp",
        "./include/Capybara/*.h",
    },

    includedirs = {
        "include/Capybara"
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
        "include/Capybara"
    },

    links = {
        "Capybara"
    },
    flags = {
        "-rdynamic",
    },
}

Project = {
    name = "test-lib",
    kind = "SharedLib",
    language = "C++",
    dialect = "20",

    files = {
        "./test/dll/test-lib.cpp",
        "./test/dll/test-lib.h",
    },

    includedirs = {
        "test/dll",
    },

    flags = {
        "-gdwarf-2",
    },

    links = {
        "base-class",
    }
}

Project = {
    name = "base-class",
    kind = "SharedLib",
    language = "C++",
    dialect = "20",

    files = {
        "./test/dll/base-class.cpp",
        "./test/dll/base-class.h",
    },

    includedirs = {
        "test/dll",
    },

    flags = {
        "-gdwarf-2",
    }
}
