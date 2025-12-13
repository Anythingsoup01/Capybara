#pragma once

#include <capybara/runtime.h>

// This will initialize a CapyDomain that will utilize 
// a custom JIT implementation for hot reloading and
// compiling files
CapyDomain* capy_jit_init(const std::string& domainName);

bool capy_jit_poll();

// This functions will set a directory that will automatically be searched
// for any file changes pertaining to .c, .cpp, .h, .hpp files and dynamically
//
// Setting recursive will set a listener in sub-directories, this can and
// will flood a debugger with processes
//
// The event listener will trigger a compilations, which can be delayed with
// capy_pause_compilation(true / false)
//
// There can only be one parent directory set
void capy_jit_set_source_path(const std::filesystem::path& sourcePath, bool recursive);

// This function is used to set both the internal BinaryPath for reloading
// binaries and the BinaryPath for JIT to compile to
void capy_jit_set_binary_path(const std::filesystem::path& binaryPath);

// This function will add a core library to both JIT and Capy for
// continuity 
void capy_jit_add_core_library(const std::filesystem::path& libPath, const std::filesystem::path& libBinaryPath);

// This function will let you provide extra functionality to the
// File Watcher On Create callback
void capy_jit_set_fw_on_create(FileCallback callback);

// This function will let you provide extra functionality to the
// File Watcher On Modify callback
void capy_jit_set_fw_on_modify(FileCallback callback);

// This function will let you provide extra functionality to the
// File Watcher On Delete callback
void capy_jit_set_fw_on_delete(FileCallback callback);

// This function will let you retrieve the JIT domain
// useful for retrieving after capy_jit_poll
CapyDomain* capy_jit_get_domain();
