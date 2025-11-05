cmake_minimum_required(VERSION 4.1.2)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Capybara LANGUAGES CXX)
set(SRCS
    "include/Capybara/Capybara.cpp"
    "include/Capybara/Runtime.cpp"
    "include/Capybara/Utility.cpp"
    "include/Capybara/Utility.h"
    "include/Capybara/cpypch.h"
    "include/Capybara/Capybara.h"
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

set(SRCS
    "./test/linux/main.cpp"
)
add_executable(test-linux-exe ${SRCS})
set_property(TARGET test-linux-exe PROPERTY CXX_STANDARD 20)

target_include_directories(test-linux-exe PRIVATE
    "test/linux"
    "include/Capybara"
)

target_link_libraries(test-linux-exe
    Capybara
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

