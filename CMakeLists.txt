set(SRCS
    "src/fswatcher/fswatcher.cpp"
    "src/util/ffi_util.cpp"
    "src/util/libelf_util.cpp"
    "src/util/fswatcher_utils.cpp"
    "src/util/string_util.cpp"
    "src/util/cxxabi_util.cpp"
    "src/capybara/runtime.cpp"
    "src/capybara/capybara.cpp"
    "include/capybara/capybara.h"
    "include/capybara/runtime.h"
    "internal/fswatcher/fswatcher.h"
    "internal/util/libelf_util.h"
    "internal/util/fswatcher_utils.h"
    "internal/util/ffi_util.h"
    "internal/util/string_util.h"
    "internal/util/cxxabi_util.h"
)
add_library(Capybara STATIC ${SRCS})
set_property(TARGET Capybara PROPERTY CXX_STANDARD 20)

target_precompile_headers(Capybara PUBLIC pch/cpypch.h)
target_include_directories(Capybara PRIVATE
    "include"
    "internal"
    "pch"
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

add_subdirectory(test/dll)
