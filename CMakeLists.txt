set(SRCS
    "Capybara/src/Capybara/JIT/JITCompiler.cpp"
    "Capybara/src/Capybara/JIT/JIT.cpp"
    "Capybara/src/Capybara/JIT/FileWatcher.cpp"
    "Capybara/src/Capybara/Core/Capybara.cpp"
    "Capybara/src/Capybara/Core/LibraryLoader.cpp"
    "Capybara/src/Capybara/Core/ObjectAllocator.cpp"
)
add_library(Capybara STATIC ${SRCS})
set_property(TARGET Capybara PROPERTY CXX_STANDARD 20)

target_precompile_headers(Capybara PUBLIC Capybara/src/Capybara/cpch.h)
target_include_directories(Capybara PRIVATE
    "Capybara/src"
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

add_subdirectory(test/script_core)
