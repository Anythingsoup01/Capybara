#pragma once

#include "Runtime.hpp"

// This function initializes our strorage, calling init basically
// just clears the storage and sets default values.
void capy_init();

/*
 *  Shutdown Capybara, used to free all loaded libraries
 */
void capy_shutdown();

// This function let's the user set a default search path for libraries
void capy_set_libraries_path(const std::filesystem::path& libPath);

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

// This function will initialize a new domain, if it exists already it'll
// return nullptr for memory security
CapyDomain* capy_init_domain(const std::string& name);

// This function is used to unload a domain and all it's libraries
void capy_unload_domain(const std::string& domainName);

// This is a utility function that dumps the contents of a given domain
// returns a string that can be printed normally or with a logging system
std::string capy_dump_domain(const std::string& domainName);

// This function is combined with the default search path to look in a
// given directory and get every library, for accurate information
// capy_unload_domain(domain*) should be called to clear the domains
// and a new domain should be created before trying to load libraries
void capy_reload_libraries_into_domain(CapyDomain* cd);

// This function is used to load libraries, used primarily by the
// capy_reload_libraries_into_domain function to emplace libraries,
// if the user want's to use a library not in the path, providing the
// path should allow the user to open it anyways due to a fallback system
// in place.
//
// If a library is intended to be a Core Library, you must call this function
// before capy_reload_libraries_into_domain, otherwise it's not going to work.
//
// Marking isCore as true will push it's complete file name into a list,
// the list can be pulled from capy_get_core_libraries_from_domain,
// which returns a vector of strings.
CapyLibrary* capy_domain_library_open(CapyDomain* cd, const std::string& libName, bool isCore);

// This function lets you retrieve a vector of library names that were marked as core,
// this is useful for making a runtime that needs base classes, I.e. MonoBehavior (for C#),
// to be linked across multiple libraries.
std::vector<std::string> capy_get_core_libraries_from_domain(const std::string& domainName);

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

// This function lets you get the type name of well types, you can also add your
// own by overloading this function as it's a template, the default types are as followed:
//
// It is recommended to use types from <cstdint> for complete cross platform ablility
//
// double   -> Double
// float    -> Float
//
// int16_t  -> Int16
// int32_t  -> Int32
// int64_t  -> Int64
//
// uint16_t -> UInt16
// uint32_t -> UInt32
// uint16_t -> UInt64
template<typename T> constexpr const char* capy_type_name();

// This function lets you set up default types you want the libraries to see
template<typename List>
void capy_register_internal_types();

// This function let's you add internal functions, this should be handled after
// adding / reloading all libraries to ensure everything gets the correct symbol
void capy_add_internal_call(const std::string& name, void* functionSymbol);


void capy_add_type_setter(const std::string& name, FieldSetterFunc setter);
