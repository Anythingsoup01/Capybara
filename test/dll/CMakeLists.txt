add_library(test SHARED)

set_property(TARGET test PROPERTY CXX_STANDARD 20)
target_sources(test PRIVATE
src/base.cpp
src/test.cpp
)

target_compile_options(test PRIVATE
-gdwarf-4
)



