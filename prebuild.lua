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

	pch = "Capybara/src/Capybara/cpch.h",

	files = {
		"Capybara/src/Capybara/**.cpp",
	},

	includedirs = {
    "Capybara/src"
	},

	links = {
		"dl",
		"elf",
		"dwarf",
		"ffi",
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
		"./test/main.cpp",
	},

	includedirs = {
		"test",
		"Capybara/src",
	},

	links = {
		"Capybara",
	},
	flags = {
		"-rdynamic",
	},
}

External = "test/script_core"
