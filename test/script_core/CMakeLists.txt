set(SRCS
    "src/Transform.cpp"
    "src/internal_calls.cpp"
    "src/Object.cpp"
)
add_library(ScriptCore SHARED ${SRCS})
set_property(TARGET ScriptCore PROPERTY CXX_STANDARD 20)

target_include_directories(ScriptCore PRIVATE
    "src"
)

target_compile_options(ScriptCore PRIVATE
    -fPIC
    -gdwarf-4
)

