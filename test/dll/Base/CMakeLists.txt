set(SRCS
    "base-class.cpp"
    "internal_calls.cpp"
    "base-class.h"
    "internal_calls.h"
)
add_library(base-class SHARED ${SRCS})
set_property(TARGET base-class PROPERTY CXX_STANDARD 20)

target_compile_options(base-class PRIVATE
    -gdwarf-4
)

