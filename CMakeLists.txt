cmake_minimum_required(VERSION 3.28.3)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(Capybara LANGUAGES CXX)
set(SRCS
    "include/Capybara/Capybara.cpp"
    "include/Capybara/Capybara.h"
)
add_library(Capybara STATIC ${SRCS})
set_property(TARGET Capybara PROPERTY CXX_STANDARD 20)

target_include_directories(Capybara PRIVATE
    "include/Capybara"
)

target_link_libraries(Capybara
    dl
    elf
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

