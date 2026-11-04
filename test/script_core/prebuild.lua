Project = {
  name = "ScriptCore",
  kind = "StaticLib",
  language = "C++",
  dialect = "20",

  files = {
    "src/**.cpp",
  },
  includedirs = {
    "src"
  },
	flags = {
		"-rdynamic",
	},
}
