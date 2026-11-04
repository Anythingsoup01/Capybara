Project = {
	name = "test",
	kind = "SharedLib",
	language = "C++",
	dialect = "20",

	files = {
		"src/**.cpp",
	},

	flags = {
		"-gdwarf-4",
	},
}
