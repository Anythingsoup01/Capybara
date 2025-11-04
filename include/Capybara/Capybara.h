#pragma once

#include "Runtime.hpp"

/*
 *  Shutdown Capybara, used to free all loaded libraries
 */
void capy_shutdown();

// This function let's the user set a default search path for libraries
void capy_set_libraries_path(const std::filesystem::path& libPath);

// This function will initialize a new domain, if it exists already it'll
// return nullptr for memory security
CapyDomain* capy_init_domain(const std::string& name);

// This function is used to unload a domain and all it's libraries
void capy_unload_domain(CapyDomain* cd);

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
CapyLibrary* capy_domain_library_open(CapyDomain* cd, const std::string& libName);

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
void capy_field_data_get_from_class(CapyClass* cc, const std::string& fieldName, void* value);

// This function is used to get the data at a given field
void capy_field_data_get_from_field(CapyField* cf, void* value);

// This function is used to set the data at a given field in a class
// as long as you know the name of the field
void capy_field_data_set_from_class(CapyClass* cc, const std::string& fieldName, void* value);

// This function is used to set the data at a give field in a class
void capy_field_data_set_from_field(CapyField* cf, void* value);

// This function is used to call a given method with given values
void* capy_function_call_from_method(CapyMethod* cm, const std::vector<RuntimeValue>& values);

