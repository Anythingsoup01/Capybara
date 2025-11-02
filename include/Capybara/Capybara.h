#pragma once

#include <filesystem>

#include "Runtime.hpp"

/*
 *  Initialize Capybara, not so needed in this early version,
 *  but as time goes on, we'll need it for memory allocations
 */
void capy_init();

/*
 *  Shutdown Capybara, used to free all loaded libraries
 */
void capy_shutdown();

CapyDomain* capy_init_domain(const std::string& name);

CapyLibrary* capy_domain_library_open(CapyDomain* cd, const std::filesystem::path& libPath);

CapyImage* capy_library_get_image(CapyLibrary* cl);

CapyClass* capy_class_from_name(CapyImage* ci, const std::string& nameSpace, const std::string& className);

CapyMethod* capy_method_from_class(CapyClass* cc, const std::string& functionName);

CapyField* capy_field_from_class(CapyClass* cc, const std::string& fieldName);


void capy_field_data_get_from_class(CapyClass* cc, const std::string& fieldName, void* value);

void capy_field_data_get_from_field(CapyField* cf, const std::string& fieldName, void* value);


void capy_field_data_set_from_class(CapyClass* cc, const std::string& fieldName, void* value);

void capy_field_data_set_from_field(CapyField* cf, const std::string& fieldName, void* value);

void* capy_function_call_from_method(CapyMethod* cm, const std::vector<RuntimeValue>& values);

