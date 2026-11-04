cmake_minimum_required(VERSION 4.4.5)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Capybara LANGUAGES CXX)
add_compile_options(
    -fPIC
)

add_library(Capybara STATIC)

set_property(TARGET Capybara PROPERTY CXX_STANDARD 20)
target_sources(Capybara PRIVATE
Capybara/src/Capybara/JIT/JITCompiler.cpp
Capybara/src/Capybara/JIT/JIT.cpp
Capybara/src/Capybara/JIT/FileWatcher.cpp
Capybara/src/Capybara/Core/Capybara.cpp
Capybara/src/Capybara/Core/LibraryLoader.cpp
Capybara/src/Capybara/Core/ObjectAllocator.cpp
)

target_precompile_headers(Capybara PUBLIC Capybara/src/Capybara/cpch.h)

target_include_directories(Capybara PRIVATE
Capybara/src/Capybara
)

target_link_libraries(Capybara
dl
elf
dwarf
ffi
)

target_compile_options(Capybara PRIVATE
-rdynamic
)


add_executable(test-linux-exe)

set_property(TARGET test-linux-exe PROPERTY CXX_STANDARD 20)
target_sources(test-linux-exe PRIVATE
"./test/main.cpp"
)

target_include_directories(test-linux-exe PRIVATE
test
Capybara/src/Capybara
)

target_link_libraries(test-linux-exe
Capybara
)

target_compile_options(test-linux-exe PRIVATE
-rdynamic
)


add_subdirectory(test/script_core)

