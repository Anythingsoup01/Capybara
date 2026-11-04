add_library(ScriptCore STATIC)

set_property(TARGET ScriptCore PROPERTY CXX_STANDARD 20)
target_sources(ScriptCore PRIVATE
src/Transform.cpp
src/Object.cpp
)

target_include_directories(ScriptCore PRIVATE
src
)

target_compile_options(ScriptCore PRIVATE
-rdynamic
)



