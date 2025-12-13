cmake_minimum_required(VERSION 4.2.1)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Capybara LANGUAGES CXX)
add_compile_options(
    -fPIC
)

set(SRCS
    "src/fswatcher/fswatcher.cpp"
    "src/util/ffi_util.cpp"
    "src/util/libelf_util.cpp"
    "src/util/fswatcher_utils.cpp"
    "src/util/string_util.cpp"
    "src/util/cxxabi_util.cpp"
    "src/capybara/runtime.cpp"
    "src/capybara/capybara.cpp"
    "src/jit/jit.cpp"
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

set(SRCS
    "./test/linux/main.cpp"
)
add_executable(test-linux-exe ${SRCS})
set_property(TARGET test-linux-exe PROPERTY CXX_STANDARD 20)

target_include_directories(test-linux-exe PRIVATE
    "test/linux"
    "include"
)

target_link_libraries(test-linux-exe
    Capybara
)

target_compile_options(test-linux-exe PRIVATE
    -rdynamic
)

add_subdirectory(test/dll)
