Workspace = {
    name = "Capybara"
}

Project = {
    name = "Capybara",
    kind = "StaticLib",
    language = "C++",
    dialect = "20",

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
        "ffi"
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
}
