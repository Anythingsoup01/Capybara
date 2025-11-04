#pragma once

#include "Runtime.hpp"

// This function is meant to demaingle c++ mangled names
std::string demangle_symbol_name(const char* name);

// This function lets you compare multiple strings to a main string
// however this one doesn't check length.
bool strs_n_equal(const std::string& mainString, const std::vector<std::string>& comparedTo);

// This function let's you check against a single string while
// also checking the length of the strings, if they aren't equal
// this will return false
bool str_n_equal(const std::string& mainString, const std::string& comparedTo);

// This function will let you convert a string into the relative
// value type, this returns ValueType::VOID if there is no match
ValueType string_to_value_type(const std::string& value);

// This function will return the corresponding ffi_type to the
// ValueType that is provided
ffi_type* get_ffi_type_p(ValueType type);

// This function converts a RuntimeValue into a valid ffi pointer
void* get_ffi_arg_p(RuntimeValue& val);

// This function will return the corresponding size of the ValueType
size_t type_size(ValueType type);

// This function will get the name at any give die, if no name is
// found <anon> will be returned
std::string get_short_name(const dwarf::die& die);

// This function will recursively build the ValueType as a string
std::string resolve_type(const dwarf::die& type_die);

// This function will give the return type name of a give dwarf::die
std::string get_return_type(const dwarf::die& die);

