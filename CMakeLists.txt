set(SRCS
    "include/Capybara/Utility.cpp"
    "include/Capybara/Capybara.cpp"
    "include/Capybara/Runtime.cpp"
    "include/Capybara/Capybara.h"
    "include/Capybara/cpypch.h"
    "include/Capybara/Utility.h"
)
add_library(Capybara STATIC ${SRCS})
set_property(TARGET Capybara PROPERTY CXX_STANDARD 20)

target_precompile_headers(Capybara PUBLIC include/Capybara/cpypch.h)
target_include_directories(Capybara PRIVATE
    "include/Capybara"
)

target_link_libraries(Capybara
    dl
    elf
    elf++
    dwarf++
    ffi
)

target_compile_options(Capybara PRIVATE
    -rdynamic
)

