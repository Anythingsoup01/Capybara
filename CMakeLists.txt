cmake_minimum_required(VERSION 4.2.1)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Capybara LANGUAGES CXX)
add_compile_options(
    -fPIC
)

set(SRCS
    "src/util/ffi_util.cpp"
    "src/util/libelf_util.cpp"
    "src/util/string_util.cpp"
    "src/util/cxxabi_util.cpp"
    "src/capybara/runtime.cpp"
    "src/capybara/capybara.cpp"
    "include/capybara/capybara.h"
    "include/capybara/runtime.h"
    "util/libelf_util.h"
    "util/ffi_util.h"
    "util/string_util.h"
    "util/cxxabi_util.h"
)
add_library(Capybara STATIC ${SRCS})
set_property(TARGET Capybara PROPERTY CXX_STANDARD 20)

target_precompile_headers(Capybara PUBLIC pch/cpypch.h)
target_include_directories(Capybara PRIVATE
    "include"
    "util"
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

set(SRCS
    "./test/dll/test-lib.cpp"
    "./test/dll/test-lib.h"
)
add_library(test-lib SHARED ${SRCS})
set_property(TARGET test-lib PROPERTY CXX_STANDARD 20)

target_include_directories(test-lib PRIVATE
    "test/dll"
)

target_link_libraries(test-lib
    base-class
)

target_compile_options(test-lib PRIVATE
    -gdwarf-4
    -g
)

set(SRCS
    "./test/dll/base-class.cpp"
    "./test/dll/base-class.h"
    "./test/dll/internal_calls.cpp"
    "./test/dll/internal_calls.h"
)
add_library(base-class SHARED ${SRCS})
set_property(TARGET base-class PROPERTY CXX_STANDARD 20)

target_include_directories(base-class PRIVATE
    "test/dll"
)

target_compile_options(base-class PRIVATE
    -gdwarf-4
    -g
)

