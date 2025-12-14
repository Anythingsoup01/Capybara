#pragma once

#include "runtime.h"

// This will initialize a CapyDomain that will utilize
// a custom JIT implementation for hot reloading and
// compiling files
//
// This also applies any ignored names/class names/ namespaces
CapyDomain* capy_jit_init();

// This function clears all the storage and shutdown JIT
void capy_jit_shutdown();

// This function checks internally if files need compiled
// blocking this with a bool can delay compilation
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

// This function sets the default search path for binaries along with
// where JIT will compile them to
void capy_jit_set_binary_path(const std::filesystem::path& binaryPath);

// This function sets the include path for you core binary / binaries
// there can only be one, so core binaries must be stored together
void capy_jit_set_core_bin_include_path(const std::filesystem::path& includePath);

// This function lets you specify a namespace you want to ignore,
// narrowing down any extra namespaces you want to get rid of,
// calling this twice will only increase the amount of namespaces it's ignoring,
// you should not call this in an infinite loop.
void capy_set_ignored_namespace(const std::vector<std::string>& ignoredNamespace);

// This function will ignore completely empty namespaces, however, if there
// is a class name it will treat it as a namespace
void capy_set_ignore_empty_namespace(bool active);

// This function lets you specify any classes you'd like to ignore,
// I.e. Helper classes, not all that usefull but wanted some continuity
// between the namespace and classname portion of our symbols
void capy_set_ignored_classname(const std::vector<std::string>& ignoredClassnames);

// This function is used to unload a domain and all it's libraries
// by using the domain's name as opposed to it's hash value
void capy_reload_domain();

// This is a utility function that dumps the contents of a given domain
// returns a string that can be printed normally or with a logging system
std::string capy_dump_domain();

// This function is combined with the default search path to look in a
// given directory and get every library, for accurate information
// capy_unload_domain(domain*) should be called to clear the domains
// and a new domain should be created before trying to load libraries
void capy_reload_libraries_into_domain();

// This function is used to open non-core libraries
// if you want to add a core library use
// capy_domain_core_library_open instead
CapyLibrary* capy_domain_library_open(const std::string& binName);

// This function is used to open core libraries, core libraries
// get stored in the storage and get linked to all libraries when
// recompiled
CapyLibrary* capy_domain_core_library_open(const std::filesystem::path& binPath);

// This function lets you retrieve a vector of library names that were marked as core,
// this is useful for making a runtime that needs base classes, I.e. MonoBehavior (for C#),
// to be linked across multiple libraries.
std::vector<std::string> capy_get_core_libraries_from_domain();

// This function is used to get the image of a given library
CapyImage* capy_library_get_image(CapyLibrary* cl);

// This function is used to get a class from a given image,
// Provide the namespace and class name to retrieve the wanted class
CapyClass* capy_class_from_name(CapyImage* ci, const std::string& nameSpace, const std::string& className);

// This function is used to get a method from a given class,
// all that is needed to know is the class name and an understanding of how many
// parameters are in the class along with the actual types for the parameters
CapyMethod* capy_method_from_class(CapyClass* cc, const std::string& functionName);

// This function is used to get a field from a given "class", as it currently stands
// you aren't able to pull out actual class objects, just global objects
CapyField* capy_field_from_class(CapyClass* cc, const std::string& fieldName);

// This function is used to get the data at a given field in a class
// as long as you know the name of the field
void capy_field_data_get(void* instance, CapyClass* cc, const std::string& fieldName, void* value);

// This function is used to get the data at a given field
void capy_field_data_get(void* instance, CapyField* cf, void* value);

// This function is used to set the data at a given field in a class
// as long as you know the name of the field
void capy_field_data_set(void* instance, CapyClass* cc, const std::string& fieldName, void* value);

// This function is used to set the data at a give field in a class
void capy_field_data_set(void* instance, CapyField* cf, void* value, int valueSizeOverride = -1);

// This function is used to call a given method with given values
void* capy_function_call_from_method(CapyMethod* cm, const std::vector<RuntimeValue>& values);

// This function let's you add internal functions, this can be done both before or
// after loading libraries into the domain
void capy_add_internal_call(const std::string& name, void* functionSymbol);

// This function let's the user define a specific setter for a type, this allows
// users to do certain type conversions needed for classes / structs
void capy_add_type_setter(const std::string& name, FieldSetterFunc setter);

// This function lets you create a custom callback to handle
// the given file system events:
//      FileEventType::Create
//      FileEventType::Modify
//      FileEventType::Delete
// This should return the std::filesystem::path variable if the file is
// one that should be compiled.
void capy_jit_set_fs_event_callback(FileEventCallback callback);

// This function is to retrieve the active domain, which stores
// all the data for loaded classes, methods, and fields
CapyDomain* capy_get_root_domain();


void capy_jit_update_fs_event_watcher();

void capy_jit_set_ignore_hidden_paths(bool active);
