#pragma once

#include "capybara/runtime.h"

#include <ffi.h>

// This function will return the corresponding ffi_type to the
// ValueType that is provided
ffi_type* get_ffi_type_p(ValueType type);

// This function converts a RuntimeValue into a valid ffi pointer
void* get_ffi_arg_p(RuntimeValue& val);
